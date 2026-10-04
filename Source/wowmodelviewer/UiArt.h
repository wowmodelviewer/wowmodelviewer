/*
 * UiArt.h
 *
 * The shell's chrome, drawn through wxAUI's art providers so docking, floating, resizing and the
 * saved layouts stay wxAUI's own:
 *
 *   UiDockArt     the pane captions (flat, a quiet title, a hairline under it, a drawn close button),
 *                 the sashes and the pane borders. The pane holding the keyboard has its title in the
 *                 text colour and an accent line under it; the others are quiet.
 *   UiToolBarArt  a wxAuiToolBar drawn flat: a mode selector's radio tools as one segmented control
 *                 with the selected segment in the accent, utility tools as quiet "ghost" buttons that
 *                 only show a fill under the mouse, panel toggles with a neutral pressed state, group
 *                 labels in small capitals, hairline separators. Tools may carry an icon (UiIcons).
 *
 * Every colour comes from UiStyle::palette(), read when drawing. A wxAuiToolBar keeps its background in
 * a bitmap, made again only for a resize or Windows' colour change, so a palette change while running
 * also sends the toolbars that event (ModelViewer::ApplyTheme); UpdateColoursFromSystem puts the
 * palette back if Windows' colours change while running.
 */

#ifndef UIART_H
#define UIART_H

#include <map>

#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>

#include "UiIcons.h"

class UiDockArt : public wxAuiDefaultDockArt
{
public:
  explicit UiDockArt(const wxWindow * frame);

  wxAuiDockArt * Clone() wxOVERRIDE;
  void UpdateColoursFromSystem() wxOVERRIDE;
  // Takes the colours of the palette in use (after a theme change).
  void applyPalette();

  void DrawSash(wxDC & dc, wxWindow * window, int orientation, const wxRect & rect) wxOVERRIDE;
  void DrawBackground(wxDC & dc, wxWindow * window, int orientation, const wxRect & rect) wxOVERRIDE;
  void DrawCaption(wxDC & dc, wxWindow * window, const wxString & text, const wxRect & rect,
                   wxAuiPaneInfo & pane) wxOVERRIDE;
  void DrawPaneButton(wxDC & dc, wxWindow * window, int button, int buttonState, const wxRect & rect,
                      wxAuiPaneInfo & pane) wxOVERRIDE;

  // The pane whose window holds the keyboard ("" for none): its caption reads as active. Returns
  // whether that changed (the caller repaints the captions).
  bool SetActivePane(const wxString & name);
  const wxString & activePane() const { return m_activePane; }

private:
  bool isActive(const wxAuiPaneInfo & pane) const;
  void drawCaptionLine(wxDC & dc, const wxRect & rect, bool active) const;

  const wxWindow * m_frame;
  wxString m_activePane;
};

class UiToolBarArt : public wxAuiGenericToolBarArt
{
public:
  // How a tool is drawn. Auto: radio tools are segments, check tools toggles, the rest ghosts.
  enum class Role { Auto, Segment, Ghost, Toggle };

  // bottomLine: a hairline under the bar (it sits on a pane's content).
  explicit UiToolBarArt(bool bottomLine = true);

  wxAuiToolBarArt * Clone() wxOVERRIDE;
  void UpdateColoursFromSystem() wxOVERRIDE;

  void SetToolIcon(int toolId, UiIcon icon) { m_icons[toolId] = icon; }
  void SetToolRole(int toolId, Role role) { m_roles[toolId] = role; }

  void DrawBackground(wxDC & dc, wxWindow * wnd, const wxRect & rect) wxOVERRIDE;
  void DrawPlainBackground(wxDC & dc, wxWindow * wnd, const wxRect & rect) wxOVERRIDE;
  void DrawLabel(wxDC & dc, wxWindow * wnd, const wxAuiToolBarItem & item, const wxRect & rect) wxOVERRIDE;
  void DrawButton(wxDC & dc, wxWindow * wnd, const wxAuiToolBarItem & item, const wxRect & rect) wxOVERRIDE;
  void DrawSeparator(wxDC & dc, wxWindow * wnd, const wxRect & rect) wxOVERRIDE;
  wxSize GetLabelSize(wxReadOnlyDC & dc, wxWindow * wnd, const wxAuiToolBarItem & item) wxOVERRIDE;
  wxSize GetToolSize(wxReadOnlyDC & dc, wxWindow * wnd, const wxAuiToolBarItem & item) wxOVERRIDE;

private:
  Role roleOf(const wxAuiToolBarItem & item) const;
  UiIcon iconOf(const wxAuiToolBarItem & item) const;
  // A segment's neighbours in its group (adjacent segments).
  bool segmentNeighbour(wxWindow * wnd, const wxAuiToolBarItem & item, int step) const;

  bool m_bottomLine;
  std::map<int, UiIcon> m_icons;
  std::map<int, Role> m_roles;
};

#endif // UIART_H
