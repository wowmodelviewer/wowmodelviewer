/*
 * VoiceLinesDialog.cpp -- see VoiceLinesDialog.h.
 */
#include "VoiceLinesDialog.h"

#include <wx/filedlg.h>

#include <cstdio>
#include <set>

#include "AudioPlayer.h"
#include "Game.h"         // GAMEDIRECTORY
#include "GameFile.h"
#include "globalvars.h"

int VoiceLinesDialog::s_sessionVolume = 100;

// VoiceLineEntry fields are Qt QStrings (resolved in the wow lib); convert to wxString at the UI boundary.
static wxString q2w(const QString & s)
{
  return wxString::FromUTF8(s.toUtf8().constData());
}

enum
{
  ID_VL_SOURCE = wxID_HIGHEST + 501,
  ID_VL_CATEGORY,
  ID_VL_LIST,
  ID_VL_PLAY,
  ID_VL_STOP,
  ID_VL_VOLUME,
  ID_VL_EXPORT
};

BEGIN_EVENT_TABLE(VoiceLinesDialog, wxDialog)
  EVT_CHOICE(ID_VL_SOURCE, VoiceLinesDialog::OnSource)
  EVT_CHOICE(ID_VL_CATEGORY, VoiceLinesDialog::OnCategory)
  EVT_LISTBOX_DCLICK(ID_VL_LIST, VoiceLinesDialog::OnLineActivate)
  EVT_BUTTON(ID_VL_PLAY, VoiceLinesDialog::OnPlay)
  EVT_BUTTON(ID_VL_STOP, VoiceLinesDialog::OnStop)
  EVT_SLIDER(ID_VL_VOLUME, VoiceLinesDialog::OnVolume)
  EVT_BUTTON(ID_VL_EXPORT, VoiceLinesDialog::OnExport)
  EVT_BUTTON(wxID_CLOSE, VoiceLinesDialog::OnCloseButton)
  EVT_CLOSE(VoiceLinesDialog::OnClose)
END_EVENT_TABLE()

// Infer a file extension for a raw audio buffer whose name/extension is unknown, from its header magic.
static wxString inferAudioExtension(const unsigned char * buf, size_t len)
{
  if (buf && len >= 4 && buf[0] == 'O' && buf[1] == 'g' && buf[2] == 'g' && buf[3] == 'S')
    return wxT("ogg");
  if (buf && len >= 12 && buf[0] == 'R' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == 'F' &&
      buf[8] == 'W' && buf[9] == 'A' && buf[10] == 'V' && buf[11] == 'E')
    return wxT("wav");
  if (buf && len >= 3 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3')
    return wxT("mp3");
  if (buf && len >= 2 && buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0) // MP3 frame sync
    return wxT("mp3");
  return wxEmptyString;
}

static wxString sanitizeFileName(const wxString & in)
{
  wxString out = in;
  const wxString bad = wxT("\\/:*?\"<>|");
  for (size_t i = 0; i < out.length(); ++i)
    if (bad.Find(out[i]) != wxNOT_FOUND)
      out[i] = wxT('_');
  return out;
}

VoiceLinesDialog::VoiceLinesDialog(wxWindow * parent, const wxString & creatureName,
                                   const std::vector<VoiceLineEntry> & soundLines,
                                   const std::vector<VoiceLineEntry> & voiceFolderLines)
  : wxDialog(parent, wxID_ANY, wxT("Voice Lines - ") + creatureName, wxDefaultPosition,
             wxSize(480, 440), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_creatureName(creatureName), m_soundLines(soundLines), m_voiceFolderLines(voiceFolderLines),
    m_player(new AudioPlayer())
{
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);

  // Source row: Creature Sounds (V1 CreatureSoundData) vs Creature Voice Lines (V2 sound-folder vo_*).
  wxBoxSizer * srcRow = new wxBoxSizer(wxHORIZONTAL);
  srcRow->Add(new wxStaticText(this, wxID_ANY, wxT("Source:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_source = new wxChoice(this, ID_VL_SOURCE);
  m_source->Append(wxT("Creature Sounds"));      // index 0 -> m_soundLines
  m_source->Append(wxT("Creature Voice Lines")); // index 1 -> m_voiceFolderLines
  m_source->SetSelection(!m_soundLines.empty() ? 0 : (!m_voiceFolderLines.empty() ? 1 : 0));
  srcRow->Add(m_source, 1, wxEXPAND);
  top->Add(srcRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

  // Category filter row (rebuilt per source)
  wxBoxSizer * catRow = new wxBoxSizer(wxHORIZONTAL);
  catRow->Add(new wxStaticText(this, wxID_ANY, wxT("Category:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_category = new wxChoice(this, ID_VL_CATEGORY);
  catRow->Add(m_category, 1, wxEXPAND);
  top->Add(catRow, 0, wxEXPAND | wxALL, 8);

  // Line list
  m_list = new wxListBox(this, ID_VL_LIST, wxDefaultPosition, wxDefaultSize, 0, NULL, wxLB_SINGLE);
  top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  // Playback + volume row
  wxBoxSizer * ctrlRow = new wxBoxSizer(wxHORIZONTAL);
  m_play = new wxButton(this, ID_VL_PLAY, wxT("Play"));
  m_stop = new wxButton(this, ID_VL_STOP, wxT("Stop"));
  ctrlRow->Add(m_play, 0, wxRIGHT, 4);
  ctrlRow->Add(m_stop, 0, wxRIGHT, 12);
  m_volumeLabel = new wxStaticText(this, wxID_ANY, wxT("Volume: 100%"));
  ctrlRow->Add(m_volumeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_volume = new wxSlider(this, ID_VL_VOLUME, s_sessionVolume, 0, 100, wxDefaultPosition, wxSize(120, -1));
  ctrlRow->Add(m_volume, 1, wxALIGN_CENTER_VERTICAL);
  top->Add(ctrlRow, 0, wxEXPAND | wxALL, 8);

  // Export + close row
  wxBoxSizer * btnRow = new wxBoxSizer(wxHORIZONTAL);
  m_export = new wxButton(this, ID_VL_EXPORT, wxT("Export Original"));
  btnRow->Add(m_export, 0, wxRIGHT, 8);
  btnRow->AddStretchSpacer(1);
  btnRow->Add(new wxButton(this, wxID_CLOSE, wxT("Close")), 0);
  top->Add(btnRow, 0, wxEXPAND | wxALL, 8);

  // Status line
  m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

  SetSizer(top);

  m_volumeLabel->SetLabel(wxString::Format(wxT("Volume: %d%%"), s_sessionVolume));
  m_player->setVolume(s_sessionVolume / 100.0f);

  RebuildCategories();
  RefillList();
}

const std::vector<VoiceLineEntry> & VoiceLinesDialog::activeLines() const
{
  return (m_source->GetSelection() == 1) ? m_voiceFolderLines : m_soundLines;
}

void VoiceLinesDialog::RebuildCategories()
{
  m_category->Clear();
  m_category->Append(wxT("All"));
  std::set<wxString> seen;
  const std::vector<VoiceLineEntry> & lines = activeLines();
  for (size_t i = 0; i < lines.size(); ++i)
  {
    const wxString c = q2w(lines[i].category);
    if (seen.insert(c).second)
      m_category->Append(c);
  }
  m_category->SetSelection(0);
}

void VoiceLinesDialog::OnSource(wxCommandEvent &)
{
  m_player->stop();
  RebuildCategories();
  RefillList();
}

VoiceLinesDialog::~VoiceLinesDialog()
{
  if (m_player)
  {
    m_player->stop();
    delete m_player;
    m_player = NULL;
  }
}

void VoiceLinesDialog::SetStatus(const wxString & msg)
{
  if (m_status)
    m_status->SetLabel(msg);
}

void VoiceLinesDialog::RefillList()
{
  m_list->Clear();
  m_filtered.clear();
  const std::vector<VoiceLineEntry> & lines = activeLines();
  const bool all = (m_category->GetSelection() <= 0);
  const wxString cat = all ? wxString() : m_category->GetStringSelection();
  for (size_t i = 0; i < lines.size(); ++i)
  {
    if (all || q2w(lines[i].category) == cat)
    {
      m_list->Append(q2w(lines[i].label));
      m_filtered.push_back((int)i);
    }
  }
  if (!m_filtered.empty())
    m_list->SetSelection(0);

  if (lines.empty())
    SetStatus((m_source->GetSelection() == 1) ? wxT("No creature voice lines found.")
                                              : wxT("No creature sounds found."));
  else
    SetStatus(wxString::Format(wxT("%d line(s). Select one and press Play."), (int)lines.size()));
}

const VoiceLineEntry * VoiceLinesDialog::SelectedEntry() const
{
  int sel = m_list->GetSelection();
  if (sel == wxNOT_FOUND || sel < 0 || (size_t)sel >= m_filtered.size())
    return NULL;
  const std::vector<VoiceLineEntry> & lines = activeLines();
  int idx = m_filtered[sel];
  if (idx < 0 || (size_t)idx >= lines.size())
    return NULL;
  return &lines[idx];
}

void VoiceLinesDialog::OnCategory(wxCommandEvent &)
{
  RefillList();
}

void VoiceLinesDialog::OnLineActivate(wxCommandEvent &)
{
  wxCommandEvent dummy;
  OnPlay(dummy);
}

void VoiceLinesDialog::OnPlay(wxCommandEvent &)
{
  const VoiceLineEntry * e = SelectedEntry();
  if (!e)
  {
    SetStatus(wxT("Select a line first."));
    return;
  }

  GameFile * f = GAMEDIRECTORY.getFile((uint)e->fileDataId);
  if (!f)
  {
    SetStatus(wxString::Format(wxT("Audio file %d not found."), e->fileDataId));
    return;
  }
  f->open();
  if (f->isEof() || f->getSize() == 0)
  {
    SetStatus(wxString::Format(wxT("Audio file %d is empty/unreadable."), e->fileDataId));
    f->close();
    return;
  }

  const bool ok = m_player->playBytes(f->getBuffer(), f->getSize(), s_sessionVolume / 100.0f);
  f->close();
  if (ok)
    SetStatus(wxT("Playing: ") + q2w(e->label));
  else
    SetStatus(wxString::Format(wxT("Could not play %d: %s"), e->fileDataId,
                               wxString::FromUTF8(m_player->lastError()).c_str()));
}

void VoiceLinesDialog::OnStop(wxCommandEvent &)
{
  m_player->stop();
  SetStatus(wxT("Stopped."));
}

void VoiceLinesDialog::OnVolume(wxCommandEvent &)
{
  s_sessionVolume = m_volume->GetValue();
  m_volumeLabel->SetLabel(wxString::Format(wxT("Volume: %d%%"), s_sessionVolume));
  m_player->setVolume(s_sessionVolume / 100.0f);
}

void VoiceLinesDialog::OnExport(wxCommandEvent &)
{
  const VoiceLineEntry * e = SelectedEntry();
  if (!e)
  {
    SetStatus(wxT("Select a line first."));
    return;
  }

  GameFile * f = GAMEDIRECTORY.getFile((uint)e->fileDataId);
  if (!f)
  {
    SetStatus(wxString::Format(wxT("Audio file %d not found."), e->fileDataId));
    return;
  }
  f->open();
  if (f->isEof() || f->getSize() == 0)
  {
    SetStatus(wxString::Format(wxT("Audio file %d is empty/unreadable."), e->fileDataId));
    f->close();
    return;
  }

  // Preserve the real extension if the file has a listfile name; otherwise infer from the header.
  wxString ext;
  wxString fullname = q2w(f->fullname());
  int dot = fullname.Find(wxT('.'), true);
  if (dot != wxNOT_FOUND && dot + 1 < (int)fullname.length())
    ext = fullname.Mid(dot + 1).Lower();
  if (ext.IsEmpty() || ext.Find(wxT('/')) != wxNOT_FOUND || ext.length() > 4)
    ext = inferAudioExtension(f->getBuffer(), f->getSize());
  if (ext.IsEmpty())
    ext = wxT("bin");

  // Default filename: for a folder-matched voice line keep its real name (vo_73_aggramar_01_m); otherwise
  // fall back to CreatureName_Category_SoundKitID_FileDataID.
  wxString base;
  wxString wpath = q2w(e->filePath);
  if (!wpath.IsEmpty())
  {
    int sl = wpath.Find(wxT('/'), true);
    wxString bn = (sl != wxNOT_FOUND) ? wpath.Mid(sl + 1) : wpath;
    int d = bn.Find(wxT('.'), true);
    if (d != wxNOT_FOUND)
      bn = bn.Mid(0, d);
    base = sanitizeFileName(bn);
  }
  else
  {
    wxString cname = m_creatureName.IsEmpty() ? wxString(wxT("Creature")) : m_creatureName;
    wxString wcat = q2w(e->category);
    base = sanitizeFileName(wxString::Format(wxT("%s_%s_%d_%d"),
        cname, wcat, e->soundKitId, e->fileDataId));
  }

  wxString path = wxFileSelector(wxT("Export original audio"), wxGetCwd(), base + wxT(".") + ext, ext,
      ext.Upper() + wxT(" files (*.") + ext + wxT(")|*.") + ext + wxT("|All files (*.*)|*.*"),
      wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);

  if (path.IsEmpty())
  {
    f->close();
    return; // user cancelled
  }

  FILE * out = fopen(path.mb_str(), "wb");
  if (out)
  {
    fwrite(f->getBuffer(), 1, f->getSize(), out);
    fclose(out);
    SetStatus(wxT("Exported: ") + path);
  }
  else
  {
    SetStatus(wxT("Export failed (could not write file)."));
  }
  f->close();
}

void VoiceLinesDialog::OnCloseButton(wxCommandEvent &)
{
  if (m_player)
    m_player->stop();
  EndModal(wxID_CLOSE);
}

void VoiceLinesDialog::OnClose(wxCloseEvent &)
{
  if (m_player)
    m_player->stop();
  EndModal(wxID_CLOSE);
}
