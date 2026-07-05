/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

#include "ImageSequenceDialog.h"

#include "modelviewer.h"
#include "modelcanvas.h"
#include "WoWModel.h"
#include "AnimManager.h"

#include <wx/spinctrl.h>
#include <wx/filepicker.h>
#include <wx/stdpaths.h>
#include <wx/statline.h>

enum
{
  ID_ISD_FORMAT = wxID_HIGHEST + 5200,
  ID_ISD_RESPRESET,
  ID_ISD_FPS,
  ID_ISD_RANGE,
  ID_ISD_KEEPASPECT,
  ID_ISD_WIDTH,
  ID_ISD_HEIGHT,
};

BEGIN_EVENT_TABLE(ImageSequenceDialog, wxDialog)
  EVT_CHOICE(ID_ISD_FORMAT,    ImageSequenceDialog::onFormat)
  EVT_CHOICE(ID_ISD_RESPRESET, ImageSequenceDialog::onResolutionPreset)
  EVT_CHOICE(ID_ISD_FPS,       ImageSequenceDialog::onFpsChoice)
  EVT_CHOICE(ID_ISD_RANGE,     ImageSequenceDialog::onRangeChoice)
  EVT_CHECKBOX(ID_ISD_KEEPASPECT, ImageSequenceDialog::onKeepAspect)
  EVT_SPINCTRL(ID_ISD_WIDTH,   ImageSequenceDialog::onWidth)
  EVT_SPINCTRL(ID_ISD_HEIGHT,  ImageSequenceDialog::onHeight)
END_EVENT_TABLE()

static wxString sanitize(const wxString & s)
{
  wxString r = s;
  r.Replace(wxT("/"), wxT("_")); r.Replace(wxT("\\"), wxT("_"));
  r.Replace(wxT(" "), wxT("_")); r.Replace(wxT(":"), wxT("_"));
  r.Replace(wxT("*"), wxT("_")); r.Replace(wxT("?"), wxT("_"));
  r.Replace(wxT("\""), wxT("_")); r.Replace(wxT("<"), wxT("_"));
  r.Replace(wxT(">"), wxT("_")); r.Replace(wxT("|"), wxT("_"));
  if (r.IsEmpty()) r = wxT("frame");
  return r;
}

ImageSequenceDialog::ImageSequenceDialog(ModelViewer * parent)
  : wxDialog(parent, wxID_ANY, wxT("Export Image Sequence"), wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE),
    m_owner(parent), m_clipLengthMs(1000), m_aspect(16.0/9.0)
{
  // ---- derive defaults from the loaded model + current clip ----
  wxString charName = wxT("model"), animName = wxT("anim");
  WoWModel * mdl = parent && parent->canvas ? const_cast<WoWModel*>(parent->canvas->model()) : nullptr;
  if (mdl)
  {
    charName = sanitize(wxString(QString(mdl->name()).section('/', -1).section('\\', -1).toStdWString()));
    if (mdl->animManager)
    {
      const size_t ai = mdl->animManager->GetAnim();
      if (ai < mdl->anims.size())
      {
        m_clipLengthMs = mdl->anims[ai].length > 0 ? mdl->anims[ai].length : 1000;
        std::map<int, std::wstring> am = mdl->getAnimsMap();
        animName = sanitize(wxString(am[mdl->anims[ai].animID]));
      }
    }
  }
  const wxString defPrefix = charName + wxT("_") + animName;

  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
  wxFlexGridSizer * grid = new wxFlexGridSizer(2, 6, 8);
  grid->AddGrowableCol(1, 1);

  // Output folder
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Output folder:")), 0, wxALIGN_CENTER_VERTICAL);
  m_folder = new wxDirPickerCtrl(this, wxID_ANY, wxStandardPaths::Get().GetDocumentsDir(),
                                 wxT("Choose output folder"), wxDefaultPosition, wxSize(360,-1),
                                 wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
  grid->Add(m_folder, 1, wxEXPAND);

  // Prefix
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Filename prefix:")), 0, wxALIGN_CENTER_VERTICAL);
  m_prefix = new wxTextCtrl(this, wxID_ANY, defPrefix);
  grid->Add(m_prefix, 1, wxEXPAND);

  // Format
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Format:")), 0, wxALIGN_CENTER_VERTICAL);
  {
    wxArrayString f; f.Add(wxT("PNG (RGBA)")); f.Add(wxT("JPG (no alpha)")); f.Add(wxT("EXR (linear float)"));
    m_format = new wxChoice(this, ID_ISD_FORMAT, wxDefaultPosition, wxDefaultSize, f);
    m_format->SetSelection(0);
    grid->Add(m_format, 0);
  }

  // Resolution
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Resolution:")), 0, wxALIGN_CENTER_VERTICAL);
  {
    wxBoxSizer * rs = new wxBoxSizer(wxHORIZONTAL);
    wxArrayString p; p.Add(wxT("1920 x 1080")); p.Add(wxT("2560 x 1440")); p.Add(wxT("3840 x 2160")); p.Add(wxT("Viewport")); p.Add(wxT("Custom"));
    m_resPreset = new wxChoice(this, ID_ISD_RESPRESET, wxDefaultPosition, wxDefaultSize, p);
    m_resPreset->SetSelection(0);
    m_width  = new wxSpinCtrl(this, ID_ISD_WIDTH,  wxT("1920"), wxDefaultPosition, wxSize(70,-1), wxSP_ARROW_KEYS, 32, 8192, 1920);
    m_height = new wxSpinCtrl(this, ID_ISD_HEIGHT, wxT("1080"), wxDefaultPosition, wxSize(70,-1), wxSP_ARROW_KEYS, 32, 8192, 1080);
    m_keepAspect = new wxCheckBox(this, ID_ISD_KEEPASPECT, wxT("keep aspect"));
    m_keepAspect->SetValue(true);
    rs->Add(m_resPreset, 0, wxRIGHT, 6);
    rs->Add(m_width, 0); rs->Add(new wxStaticText(this, wxID_ANY, wxT(" x ")), 0, wxALIGN_CENTER_VERTICAL);
    rs->Add(m_height, 0, wxRIGHT, 8); rs->Add(m_keepAspect, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(rs, 0);
  }

  // FPS
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Frame rate:")), 0, wxALIGN_CENTER_VERTICAL);
  {
    wxBoxSizer * fs = new wxBoxSizer(wxHORIZONTAL);
    wxArrayString f; f.Add(wxT("Native (60)")); f.Add(wxT("24")); f.Add(wxT("25")); f.Add(wxT("30")); f.Add(wxT("60")); f.Add(wxT("Custom"));
    m_fps = new wxChoice(this, ID_ISD_FPS, wxDefaultPosition, wxDefaultSize, f);
    m_fps->SetSelection(3); // 30
    m_fpsCustom = new wxSpinCtrl(this, wxID_ANY, wxT("30"), wxDefaultPosition, wxSize(70,-1), wxSP_ARROW_KEYS, 1, 240, 30);
    fs->Add(m_fps, 0, wxRIGHT, 6); fs->Add(m_fpsCustom, 0);
    grid->Add(fs, 0);
  }

  // Frame range
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Frame range:")), 0, wxALIGN_CENTER_VERTICAL);
  {
    wxBoxSizer * gs = new wxBoxSizer(wxHORIZONTAL);
    wxArrayString r; r.Add(wxT("Full animation")); r.Add(wxT("Custom range (ms)"));
    m_range = new wxChoice(this, ID_ISD_RANGE, wxDefaultPosition, wxDefaultSize, r);
    m_range->SetSelection(0);
    m_rangeStart = new wxSpinCtrl(this, wxID_ANY, wxT("0"), wxDefaultPosition, wxSize(80,-1), wxSP_ARROW_KEYS, 0, 600000, 0);
    m_rangeEnd   = new wxSpinCtrl(this, wxID_ANY, wxString::Format(wxT("%u"), m_clipLengthMs), wxDefaultPosition, wxSize(80,-1), wxSP_ARROW_KEYS, 0, 600000, m_clipLengthMs);
    gs->Add(m_range, 0, wxRIGHT, 6);
    gs->Add(m_rangeStart, 0); gs->Add(new wxStaticText(this, wxID_ANY, wxT(" to ")), 0, wxALIGN_CENTER_VERTICAL); gs->Add(m_rangeEnd, 0);
    grid->Add(gs, 0);
  }

  // Numbering
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Numbering:")), 0, wxALIGN_CENTER_VERTICAL);
  {
    wxBoxSizer * ns = new wxBoxSizer(wxHORIZONTAL);
    ns->Add(new wxStaticText(this, wxID_ANY, wxT("padding ")), 0, wxALIGN_CENTER_VERTICAL);
    m_padding = new wxSpinCtrl(this, wxID_ANY, wxT("4"), wxDefaultPosition, wxSize(55,-1), wxSP_ARROW_KEYS, 1, 9, 4);
    ns->Add(m_padding, 0, wxRIGHT, 10);
    ns->Add(new wxStaticText(this, wxID_ANY, wxT("start # ")), 0, wxALIGN_CENTER_VERTICAL);
    m_startNum = new wxSpinCtrl(this, wxID_ANY, wxT("1"), wxDefaultPosition, wxSize(70,-1), wxSP_ARROW_KEYS, 0, 999999, 1);
    ns->Add(m_startNum, 0);
    grid->Add(ns, 0);
  }

  top->Add(grid, 0, wxEXPAND | wxALL, 12);

  m_transparent = new wxCheckBox(this, wxID_ANY, wxT("Transparent background (alpha channel - for After Effects)"));
  m_transparent->SetValue(true);
  top->Add(m_transparent, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  m_openFolder = new wxCheckBox(this, wxID_ANY, wxT("Open output folder when complete"));
  m_openFolder->SetValue(true);
  top->Add(m_openFolder, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

  top->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 10);

  SetSizerAndFit(top);
  Centre();
  updateEnabled();
}

void ImageSequenceDialog::updateEnabled()
{
  const bool customRes = (m_resPreset->GetSelection() == 4);
  m_width->Enable(customRes);
  m_height->Enable(customRes);
  m_keepAspect->Enable(customRes);
  m_fpsCustom->Enable(m_fps->GetSelection() == 5);
  const bool customRange = (m_range->GetSelection() == 1);
  m_rangeStart->Enable(customRange);
  m_rangeEnd->Enable(customRange);
  // EXR/PNG carry alpha; JPG cannot.
  m_transparent->Enable(m_format->GetSelection() != ImageSequenceExporter::FMT_JPG);
}

void ImageSequenceDialog::onFormat(wxCommandEvent &) { updateEnabled(); }
void ImageSequenceDialog::onFpsChoice(wxCommandEvent &) { updateEnabled(); }
void ImageSequenceDialog::onRangeChoice(wxCommandEvent &) { updateEnabled(); }
void ImageSequenceDialog::onKeepAspect(wxCommandEvent &) {}

void ImageSequenceDialog::onResolutionPreset(wxCommandEvent &)
{
  switch (m_resPreset->GetSelection())
  {
    case 0: m_width->SetValue(1920); m_height->SetValue(1080); break;
    case 1: m_width->SetValue(2560); m_height->SetValue(1440); break;
    case 2: m_width->SetValue(3840); m_height->SetValue(2160); break;
    case 3: // Viewport
      if (m_owner && m_owner->canvas)
      {
        wxSize sz = m_owner->canvas->GetClientSize();
        if (sz.x >= 32 && sz.y >= 32) { m_width->SetValue(sz.x); m_height->SetValue(sz.y); }
      }
      break;
    default: break; // Custom: leave as-is
  }
  m_aspect = (double)m_width->GetValue() / (double)m_height->GetValue();
  updateEnabled();
}

void ImageSequenceDialog::onWidth(wxSpinEvent &)
{
  if (m_keepAspect->IsEnabled() && m_keepAspect->GetValue())
    m_height->SetValue((int)(m_width->GetValue() / m_aspect + 0.5));
}
void ImageSequenceDialog::onHeight(wxSpinEvent &)
{
  if (m_keepAspect->IsEnabled() && m_keepAspect->GetValue())
    m_width->SetValue((int)(m_height->GetValue() * m_aspect + 0.5));
}

bool ImageSequenceDialog::getSettings(ImageSequenceExporter::Settings & out)
{
  out.folder = m_folder->GetPath();
  if (out.folder.IsEmpty())
  {
    wxMessageBox(wxT("Choose an output folder."), wxT("Export"), wxOK | wxICON_ERROR, this);
    return false;
  }
  out.prefix = sanitize(m_prefix->GetValue());
  out.format = m_format->GetSelection();
  out.padding = m_padding->GetValue();
  out.startNumber = m_startNum->GetValue();
  out.width = m_width->GetValue();
  out.height = m_height->GetValue();

  switch (m_fps->GetSelection())
  {
    case 0: out.fps = 60.0; break;
    case 1: out.fps = 24.0; break;
    case 2: out.fps = 25.0; break;
    case 3: out.fps = 30.0; break;
    case 4: out.fps = 60.0; break;
    default: out.fps = m_fpsCustom->GetValue(); break;
  }

  if (m_range->GetSelection() == 0) { out.rangeStartMs = 0; out.rangeEndMs = m_clipLengthMs; }
  else { out.rangeStartMs = m_rangeStart->GetValue(); out.rangeEndMs = m_rangeEnd->GetValue(); }

  out.transparent = m_transparent->IsEnabled() && m_transparent->GetValue();
  out.openFolderWhenDone = m_openFolder->GetValue();
  out.overwrite = false;
  return true;
}
