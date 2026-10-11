/*
 * ExportSettings.h
 *
 *  Created on: 1 may 2015
 *      Author: Jeromnimo
 *
 * The Settings pane's Export page: Models (Initial Pose Only Export) and the Texture Viewer's export folder,
 * where Quick export saves (TextureExport). The folder takes effect at once: it is checked (a folder that is
 * there or can be made, and can be written to) before it is kept, and the Texture Viewer is told.
 */

#ifndef _EXPORTSETTINGS_H_
#define _EXPORTSETTINGS_H_

// WX Headers
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

enum
{
  ID_SETTINGS_INIT_POSE_ONLY_EXPORT,
  ID_EXPORT_SETTINGS_APPLY
};

enum {
  CHECK_INIT_POSE_ONLY_EXPORT,
  NUM_SETTINGS3_CHECK
};

class ExportSettings: public wxScrolledCanvas
{
  DECLARE_CLASS(ExportSettings)
    DECLARE_EVENT_TABLE()

  wxCheckBox *chkbox[NUM_SETTINGS3_CHECK];

public:

  ExportSettings(wxWindow* parent, wxWindowID id);
  ~ExportSettings() {};

  void Update();
  // The pane was opened: the texture export folder shown and checked as it is now.
  void Opened();

  void OnButton(wxCommandEvent &event);
  void OnCheck(wxCommandEvent &event);

  // The texture export folder in use, the note under it and, when it cannot be used (or a choice was
  // refused), why.
  void showTextureFolder(const wxString & problem = wxEmptyString);
  // The texture export folder was changed (TextureExport::setFolder): shown again, and the Texture Viewer
  // told (its Quick export tooltip).
  void textureFolderChanged();

private:
  wxStaticText * m_about = nullptr;
  wxTextCtrl * m_textureFolder = nullptr;
  wxStaticText * m_textureFolderNote = nullptr;
  wxStaticText * m_textureFolderError = nullptr;
  // The notes' text, unwrapped, and the width they were last wrapped at.
  wxString m_aboutText, m_noteText, m_errorText;
  int m_wrappedAt = 0;

  // The width the notes wrap at: the page's, less its margins.
  int wrapWidth() const;
  // The notes wrapped to that width.
  void wrapNotes();

  void chooseTextureFolder();
  void resetTextureFolder();
  void openTextureFolder();
};



#endif /* _EXPORTSETTINGS_H_ */
