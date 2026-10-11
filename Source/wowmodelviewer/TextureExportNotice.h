/*
 * TextureExportNotice.h
 *
 * The Textures viewer's word on an export: a small card in the bottom-left corner of the texture preview, just
 * above the export buttons and lined up with them, that says where the file went -- "Texture saved", its name, its
 * folder, Open folder -- or why it was not saved.
 *
 * It is a child of the preview, so showing it never activates a window or takes the keyboard: browsing and
 * exporting carry on under it. One card at a time: the next export replaces it (the latest file is the one
 * shown). It goes by itself after a few seconds, and waits while the mouse is over it. Its buttons are real
 * buttons (UiButton); the keyboard they take on a click goes back where it was. It takes its colours from the
 * palette (a card: the field colour and the border), so it follows Light, Dark and high contrast.
 */

#ifndef TEXTUREEXPORTNOTICE_H
#define TEXTUREEXPORTNOTICE_H

#include <functional>

#include <QString>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/timer.h>
#include <wx/weakref.h>

class UiButton;

class TextureExportNotice : public wxWindow
{
public:
  // How long a notice stays (ms) while the mouse is not over it.
  static const int SavedMs;
  static const int FailedMs;

  // parent: the window it sits in, in the bottom-left corner; around: the colour around it there (its rounded
  // corners are painted in it).
  TextureExportNotice(wxWindow * parent, const wxColour & around);

  // A file was saved: its name, its folder and Open folder.
  void showSaved(const QString & file);
  // A save failed: a short reason and the path it concerns. folderProblem: the export folder cannot be used,
  // so Change folder (Options) is offered.
  void showFailed(const wxString & reason, const QString & path, bool folderProblem);
  void dismiss();

  bool failed() const { return m_failed; }
  const QString & file() const { return m_file; }

  // Change folder was clicked (the owner opens Options > Settings > Export).
  std::function<void()> onChangeFolder;

private:
  void showNotice(int ms);
  void place();
  void tick();
  bool hovered() const;
  void giveBackFocus();
  void rememberFocus(wxMouseEvent & event);
  void OnPaint(wxPaintEvent & event);

  wxColour m_around;
  wxStaticText * m_title = nullptr;
  wxStaticText * m_name = nullptr;
  wxStaticText * m_folder = nullptr;
  UiButton * m_open = nullptr;
  UiButton * m_change = nullptr;
  UiButton * m_close = nullptr;
  wxTimer m_timer;
  int m_remainingMs = 0;
  bool m_failed = false;
  QString m_file;                       // the file saved (or the path a failure concerns)
  wxWeakRef<wxWindow> m_focusBefore;    // where the keyboard was before a button of the notice took it
};

#endif // TEXTUREEXPORTNOTICE_H
