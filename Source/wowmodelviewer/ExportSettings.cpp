/*
 * ExportSettings.cpp
 *
 *  Created on: 1 may 2015
 *      Author: Jeromnimo
 */

#include "ExportSettings.h"

#include <QDir>
#include <QFileInfo>

#include <wx/dirdlg.h>
#include <wx/stdpaths.h>

#include "GlobalSettings.h"
#include "globalvars.h"
#include "modelviewer.h"
#include "TextureExport.h"
#include "TextureView.h"
#include "UiControls.h"
#include "UiStyle.h"

#include "logger/Logger.h"

IMPLEMENT_CLASS(ExportSettings, wxWindow)

BEGIN_EVENT_TABLE(ExportSettings, wxScrolledCanvas)
  EVT_CHECKBOX(ID_SETTINGS_INIT_POSE_ONLY_EXPORT, ExportSettings::OnCheck)
  EVT_BUTTON(ID_EXPORT_SETTINGS_APPLY, ExportSettings::OnButton)
END_EVENT_TABLE()

namespace
{
  wxString wx(const QString & s) { return wxString(s.toStdWString()); }
  QString qs(const wxString & s) { return QString::fromWCharArray(s.wc_str()); }
}

ExportSettings::ExportSettings(wxWindow* parent, wxWindowID id)
{
  if (Create(parent, id, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL, wxT("ExportSettings")) == false) {
    LOG_ERROR << "ExportSettings";
    return;
  }
  const int xs = FromDIP(UiStyle::XS), s = FromDIP(UiStyle::S), m = FromDIP(UiStyle::M), l = FromDIP(UiStyle::L);
  wxBoxSizer * column = new wxBoxSizer(wxVERTICAL);

  // Models: the model exporters (File > Export Model).
  column->Add(UiStyle::sectionHeader(this, _("Models")), 0, wxEXPAND);
  chkbox[CHECK_INIT_POSE_ONLY_EXPORT] = new wxCheckBox(this, ID_SETTINGS_INIT_POSE_ONLY_EXPORT, _("Initial Pose Only Export"));
  column->Add(chkbox[CHECK_INIT_POSE_ONLY_EXPORT], 0, wxTOP, s);

  // The Texture Viewer: where Quick export saves.
  column->Add(UiStyle::sectionHeader(this, _("Texture Viewer")), 0, wxEXPAND | wxTOP, l);
  wxStaticText * label = new wxStaticText(this, wxID_ANY, _("Texture export folder"));
  label->SetFont(UiStyle::font(UiStyle::Type::Strong));
  column->Add(label, 0, wxTOP, s);
  m_aboutText = _("Quick export saves textures here, each in its folder from the game "
                  "(creature\\bear\\bearskinblack.png).");
  m_about = UiStyle::secondaryLabel(this, m_aboutText);
  column->Add(m_about, 0, wxEXPAND | wxTOP, xs);
  m_textureFolder = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
  m_textureFolder->SetName(wxT("textureExportFolder"));
  UiSetAccessibleName(m_textureFolder, _("Texture export folder"));
  column->Add(m_textureFolder, 0, wxEXPAND | wxTOP, s);
  m_textureFolderNote = UiStyle::secondaryLabel(this, wxEmptyString);
  m_textureFolderNote->SetName(wxT("textureExportFolderNote"));
  column->Add(m_textureFolderNote, 0, wxEXPAND | wxTOP, xs);
  m_textureFolderError = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_textureFolderError->SetName(wxT("textureExportFolderError"));
  UiStyle::setRole(m_textureFolderError, UiStyle::Role::WarningText);
  m_textureFolderError->Hide();
  column->Add(m_textureFolderError, 0, wxEXPAND | wxTOP, xs);
  UiButton * browse = new UiButton(this, wxID_ANY, _("Browse..."), UiButton::Kind::Secondary);
  browse->SetName(wxT("textureExportFolderBrowse"));
  browse->SetToolTip(_("Choose another folder for Quick export"));
  UiButton * reset = new UiButton(this, wxID_ANY, _("Reset to default"), UiButton::Kind::Subtle, UiIcon::Reset);
  reset->SetName(wxT("textureExportFolderReset"));
  reset->SetToolTip(_("Back to Exports\\Textures in the viewer's own folder"));
  UiButton * open = new UiButton(this, wxID_ANY, _("Open folder"), UiButton::Kind::Subtle);
  open->SetName(wxT("textureExportFolderOpen"));
  open->SetToolTip(_("Open the texture export folder in Explorer"));
  wxBoxSizer * buttons = new wxBoxSizer(wxHORIZONTAL);
  buttons->Add(browse, 0, wxRIGHT, s);
  buttons->Add(reset, 0, wxRIGHT, s);
  buttons->Add(open, 0);
  column->Add(buttons, 0, wxTOP, s);

  // Apply keeps the Models setting for the next start (the texture export folder is kept as it is chosen).
  column->AddStretchSpacer();
  column->Add(new wxButton(this, ID_EXPORT_SETTINGS_APPLY, _("Apply")), 0, wxALIGN_RIGHT | wxTOP, m);

  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
  top->Add(column, 1, wxEXPAND | wxALL, m);
  SetSizer(top);

  browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { chooseTextureFolder(); });
  reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { resetTextureFolder(); });
  open->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { openTextureFolder(); });
  // A short Settings pane scrolls the page, as it does the General page (a plain window that scrolls, not a
  // panel: the page keeps its colours, and the keyboard is not moved to its first control).
  SetScrollRate(0, FromDIP(10));
  // The notes follow the page's width.
  Bind(wxEVT_SIZE, [this](wxSizeEvent & event) {
    event.Skip();
    if (wrapWidth() != m_wrappedAt)
      wrapNotes();
  });
  // (The folder is shown and checked when the pane is opened: Opened.)
}

void ExportSettings::Update()
{
  chkbox[CHECK_INIT_POSE_ONLY_EXPORT]->SetValue(GLOBALSETTINGS.bInitPoseOnlyExport);
}

void ExportSettings::Opened()
{
  showTextureFolder();
}

void ExportSettings::OnCheck(wxCommandEvent &event)
{
  int id = event.GetId();

  if (id==ID_SETTINGS_INIT_POSE_ONLY_EXPORT) {
    GLOBALSETTINGS.bInitPoseOnlyExport = event.IsChecked();
  }
}

void ExportSettings::OnButton(wxCommandEvent &event)
{
  if (event.GetId() == ID_EXPORT_SETTINGS_APPLY) {
    g_modelViewer->SaveSession();
  }
}

// ----------------------------------------------------------------------------------- texture export folder

void ExportSettings::showTextureFolder(const wxString & problem)
{
  const QString folder = TextureExport::folder();
  m_textureFolder->ChangeValue(wx(TextureExport::display(folder)));
  m_textureFolder->SetToolTip(m_textureFolder->GetValue());

  wxString note = TextureExport::configuredFolder().isEmpty()
                    ? _("The default folder: Exports\\Textures in the viewer's own folder.")
                    : wxString::Format(_("The default is %s."), wx(TextureExport::display(TextureExport::defaultFolder())));
  wxString error = problem;
  if (error.IsEmpty())
  {
    const TextureExport::FolderCheck check = TextureExport::checkFolder(folder);
    if (!check.ok)
      error = wx(check.error) + wxT(" ") + _("Quick export cannot save there: choose another folder.");
    else if (!check.exists)
      note += wxT(" ") + _("It is made by the first Quick export.");
  }
  m_noteText = note;
  m_errorText = error;
  m_textureFolderError->Show(!error.IsEmpty());
  wrapNotes();
}

int ExportSettings::wrapWidth() const
{
  return wxMax(FromDIP(160), GetClientSize().x - 2 * FromDIP(UiStyle::M));
}

void ExportSettings::wrapNotes()
{
  m_wrappedAt = wrapWidth();
  const std::pair<wxStaticText *, wxString> notes[] = {
    { m_about, m_aboutText }, { m_textureFolderNote, m_noteText }, { m_textureFolderError, m_errorText } };
  for (const auto & n : notes)
  {
    n.first->SetLabelText(n.second);   // as it is: a '&' in a path is not a mnemonic
    n.first->Wrap(-1);   // wxWidgets 3.3 skips a Wrap at the width it last wrapped at, whatever the text
    n.first->Wrap(m_wrappedAt);
  }
  Layout();
  FitInside();
}

void ExportSettings::chooseTextureFolder()
{
  // The dialog starts in the folder in use, or the nearest folder above it that is there.
  QString start = TextureExport::folder();
  while (!start.isEmpty() && !QFileInfo(start).isDir() && QFileInfo(start).path() != start)
    start = QFileInfo(start).path();
  wxDirDialog dialog(this, _("Choose the texture export folder"),
                     QFileInfo(start).isDir() ? wx(TextureExport::display(start)) : wxStandardPaths::Get().GetDocumentsDir(),
                     wxDD_DEFAULT_STYLE);
  if (dialog.ShowModal() != wxID_OK)
    return;
  const QString chosen = qs(dialog.GetPath());
  const TextureExport::FolderCheck check = TextureExport::checkFolder(chosen);
  if (!check.ok)
  {
    // Refused here, not at the next export: the folder in use stays.
    showTextureFolder(wx(check.error) + wxT(" ") + _("The texture export folder was not changed."));
    return;
  }
  TextureExport::setFolder(chosen);
  textureFolderChanged();
}

void ExportSettings::resetTextureFolder()
{
  TextureExport::setFolder(QString());
  textureFolderChanged();
}

void ExportSettings::openTextureFolder()
{
  const QString folder = TextureExport::folder();
  const TextureExport::FolderCheck check = TextureExport::checkFolder(folder);
  if (!check.ok)
  {
    showTextureFolder(wx(check.error));
    return;
  }
  TextureExport::openFolder(folder);   // made first when it is not there yet
  showTextureFolder();
}

void ExportSettings::textureFolderChanged()
{
  showTextureFolder();
  // At once: the next Quick export goes there, and the Texture Viewer's tooltip says so.
  if (g_modelViewer && g_modelViewer->textureView)
    g_modelViewer->textureView->quickExportFolderChanged();
}
