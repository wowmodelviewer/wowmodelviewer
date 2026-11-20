/*
 * ViewportHost.h
 *
 * The viewport's area of the main window. It shows one of three things:
 *  - the 3D view (ModelCanvas, drawn natively with Vulkan), when nothing covers it;
 *  - a notice -- a title, a few lines of detail and optionally a button -- when the viewport has something to say
 *    instead of a picture (no client loaded, content it cannot draw yet, a renderer that could not start);
 *  - another viewer's content (the Texture Viewer) in place of the 3D view.
 */

#ifndef _VIEWPORTHOST_H_
#define _VIEWPORTHOST_H_

#include <wx/panel.h>
#include <wx/timer.h>

class ModelCanvas;
class wxButton;

class ViewportHost : public wxPanel
{
  DECLARE_CLASS(ViewportHost)
  DECLARE_EVENT_TABLE()

public:
  ViewportHost(wxWindow * parent, wxWindowID id);

  // The 3D view: a child of this panel, shown while no notice or content covers it.
  void setCanvas(ModelCanvas * canvas);
  ModelCanvas * canvas() const { return m_canvas; }

  // The colour the 3D view clears to (the viewport background).
  void setBackdrop(const wxColour & colour);
  wxColour backdrop() const { return m_backdrop; }

  // A notice in place of the picture. actionId, when not 0, adds a button that posts that menu command to the
  // parent window.
  void setNotice(const wxString & title, const wxString & detail, const wxString & actionLabel = wxString(),
                 int actionId = 0);
  void clearNotice();
  bool hasNotice() const { return m_notice; }
  wxString noticeTitle() const { return m_noticeTitle; }
  wxString noticeDetail() const { return m_noticeDetail; }

  // Another viewer's window in place of the 3D view (shown with showContent).
  void setContent(wxWindow * content);
  wxWindow * content() const { return m_content; }
  void showContent(bool show);
  bool isShowingContent() const { return m_contentShown; }

  // Whether the 3D view is what is on screen.
  bool showingCanvas() const { return m_canvas && !m_notice && !m_contentShown; }

private:
  void OnSize(wxSizeEvent & event);
  void OnPaint(wxPaintEvent & event);
  void layoutNotice();
  void applyVisibility();
  wxArrayString noticeDetailLines(wxDC & dc) const;

  ModelCanvas * m_canvas = nullptr;
  wxWindow * m_content = nullptr;
  bool m_contentShown = false;
  bool m_notice = false;
  wxString m_noticeTitle, m_noticeDetail, m_noticeActionLabel;
  int m_noticeActionId = 0;
  wxButton * m_noticeButton = nullptr;
  wxColour m_backdrop;
};

#endif
