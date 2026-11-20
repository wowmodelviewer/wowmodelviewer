/*
 * ViewportHost.cpp -- see ViewportHost.h.
 */

#include "ViewportHost.h"

#include <algorithm>

#include "modelcanvas.h"
#include "modelviewer.h"
#include "UiControls.h"
#include "UiStyle.h"

IMPLEMENT_CLASS(ViewportHost, wxPanel)

BEGIN_EVENT_TABLE(ViewportHost, wxPanel)
  EVT_SIZE(ViewportHost::OnSize)
  EVT_PAINT(ViewportHost::OnPaint)
END_EVENT_TABLE()

namespace
{
  // One set of fonts for painting and for placing the button, so the two can never disagree.
  wxFont noticeTitleFont(const wxWindow * w)
  {
    wxFont f = w->GetFont();
    f.SetPointSize(f.GetPointSize() + 5);
    return f;
  }
  wxFont noticeDetailFont(const wxWindow * w)
  {
    wxFont f = w->GetFont();
    f.SetPointSize(f.GetPointSize() + 1);
    return f;
  }
}

ViewportHost::ViewportHost(wxWindow * parent, wxWindowID id)
{
  Create(parent, id, wxDefaultPosition, wxSize(640, 480), wxNO_BORDER | wxCLIP_CHILDREN, wxT("ViewportHost"));
  // OnPaint fills every pixel with the notice's dark (and a notice): no erase first.
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetBackgroundColour(UiStyle::palette().viewport);
  m_backdrop = UiStyle::palette().viewport;
}

void ViewportHost::setCanvas(ModelCanvas * canvas)
{
  m_canvas = canvas;
  if (m_canvas)
    m_canvas->SetSize(GetClientRect());
  applyVisibility();
}

void ViewportHost::setBackdrop(const wxColour & colour)
{
  if (colour == m_backdrop)
    return;
  m_backdrop = colour;
  if (m_canvas)
    m_canvas->Refresh(false);
  Refresh(false);
}

void ViewportHost::clearNotice()
{
  if (!m_notice)
    return;
  m_notice = false;
  if (m_noticeButton)
    m_noticeButton->Show(false);
  applyVisibility();
  Refresh();
}

void ViewportHost::setContent(wxWindow * content)
{
  if (m_content == content)
    return;
  if (m_content && m_contentShown)
    m_content->Hide();
  m_content = content;
  if (m_content)
  {
    m_content->SetSize(GetClientRect());
    m_content->Show(m_contentShown);
    if (m_contentShown)
      m_content->Raise();
  }
  applyVisibility();
}

void ViewportHost::showContent(bool show)
{
  if (!m_content || m_contentShown == show)
    return;
  m_contentShown = show;
  if (show)
  {
    m_content->SetSize(GetClientRect());
    m_content->Show();
    m_content->Raise();
  }
  else
  {
    // The keyboard leaves with it, or it would stay in a hidden window.
    wxWindow * focus = wxWindow::FindFocus();
    for (wxWindow * w = focus; w; w = w->GetParent())
      if (w == m_content)
      {
        SetFocusIgnoringChildren();
        break;
      }
    m_content->Hide();
  }
  applyVisibility();
  Refresh();
}

// The 3D view is a child window: shown only when it is what the viewport shows, so a notice or the content is never
// painted over by its frames.
void ViewportHost::applyVisibility()
{
  if (!m_canvas)
    return;
  const bool show = showingCanvas();
  if (m_canvas->IsShown() != show)
  {
    m_canvas->Show(show);
    if (show)
      m_canvas->SetSize(GetClientRect());
  }
}

void ViewportHost::OnSize(wxSizeEvent & event)
{
  const wxRect area = GetClientRect();
  if (m_canvas)
    m_canvas->SetSize(area);
  if (m_content)
    m_content->SetSize(area);
  layoutNotice();
  if (m_notice)
    Refresh();
  // The status bar reports the viewport's size, and this is the viewport.
  if (ModelViewer * frame = wxDynamicCast(GetParent(), ModelViewer))
    frame->UpdateCanvasStatus();
  event.Skip();
}

void ViewportHost::OnPaint(wxPaintEvent & WXUNUSED(event))
{
  wxPaintDC dc(this);
  const bool notice = m_notice && !m_noticeTitle.IsEmpty() && !m_contentShown;
  // The dark the notice's text and button are made for. With no notice the 3D view covers the panel.
  dc.SetBackground(wxBrush(UiStyle::palette().viewport));
  dc.Clear();

  if (!notice)
    return;

  // The notice, centred above its button (layoutNotice places the button).
  const wxRect area = GetClientRect();
  const wxFont titleFont = noticeTitleFont(this);
  const wxFont detailFont = noticeDetailFont(this);

  dc.SetFont(titleFont);
  const wxSize titleSize = dc.GetTextExtent(m_noticeTitle);
  dc.SetFont(detailFont);
  const wxArrayString lines = noticeDetailLines(dc);
  const int lineHeight = dc.GetCharHeight();

  const int gap = FromDIP(8);
  const bool button = m_noticeButton && m_noticeButton->IsShown();
  const int buttonHeight = button ? m_noticeButton->GetSize().y + FromDIP(20) : 0;
  const int total = titleSize.y + gap + (int)lines.size() * lineHeight + buttonHeight;
  int y = area.y + (area.height - total) / 2;

  dc.SetFont(titleFont);
  dc.SetTextForeground(UiStyle::palette().viewportText);
  dc.DrawText(m_noticeTitle, area.x + (area.width - titleSize.x) / 2, y);
  y += titleSize.y + gap;
  dc.SetFont(detailFont);
  dc.SetTextForeground(UiStyle::palette().viewportTextSecondary);
  for (size_t i = 0; i < lines.size(); i++)
  {
    const int width = dc.GetTextExtent(lines[i]).x;
    dc.DrawText(lines[i], area.x + (area.width - width) / 2, y);
    y += lineHeight;
  }
}

wxArrayString ViewportHost::noticeDetailLines(wxDC & dc) const
{
  // Explicit line breaks are kept; each paragraph is then wrapped at word boundaries to the panel
  // width less a margin. A single word wider than that (a long path) is left whole on its own line.
  wxArrayString out;
  const int maxWidth = std::max(FromDIP(160), GetClientSize().x - 2 * FromDIP(24));
  const wxArrayString paragraphs = wxSplit(m_noticeDetail, '\n', 0);
  for (size_t p = 0; p < paragraphs.size(); p++)
  {
    const wxArrayString words = wxSplit(paragraphs[p], ' ', 0);
    wxString line;
    for (size_t w = 0; w < words.size(); w++)
    {
      const wxString candidate = line.IsEmpty() ? words[w] : line + wxT(" ") + words[w];
      if (!line.IsEmpty() && dc.GetTextExtent(candidate).x > maxWidth)
      {
        out.Add(line);
        line = words[w];
      }
      else
        line = candidate;
    }
    out.Add(line);
  }
  return out;
}

void ViewportHost::setNotice(const wxString & title, const wxString & detail, const wxString & actionLabel,
                                  int actionId)
{
  const bool wantButton = actionId != 0 && !actionLabel.IsEmpty();
  if (m_notice && m_noticeTitle == title && m_noticeDetail == detail && m_noticeActionLabel == actionLabel &&
      m_noticeActionId == actionId)
    return;   // the same notice: nothing to repaint, and no timer to restart

  m_notice = true;
  m_noticeTitle = title;
  m_noticeDetail = detail;
  m_noticeActionLabel = actionLabel;
  m_noticeActionId = actionId;

  if (wantButton && !m_noticeButton)
  {
    m_noticeButton = new UiButton(this, wxID_ANY, actionLabel, UiButton::Kind::Primary);
    m_noticeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      if (m_noticeActionId == 0 || !GetParent())
        return;
      wxCommandEvent command(wxEVT_MENU, m_noticeActionId);
      wxPostEvent(GetParent(), command);
    });
  }
  if (m_noticeButton)
  {
    m_noticeButton->SetLabel(actionLabel);
    m_noticeButton->SetSize(m_noticeButton->GetBestSize());
    m_noticeButton->Show(wantButton);
  }

  applyVisibility();
  layoutNotice();
  Refresh();
}

void ViewportHost::layoutNotice()
{
  if (!m_noticeButton || !m_noticeButton->IsShown())
    return;
  const wxRect area = GetClientRect();
  wxClientDC dc(this);
  dc.SetFont(noticeTitleFont(this));
  const int titleHeight = dc.GetTextExtent(m_noticeTitle.IsEmpty() ? wxString(wxT("X")) : m_noticeTitle).y;
  dc.SetFont(noticeDetailFont(this));
  const int detailHeight = (int)noticeDetailLines(dc).size() * dc.GetCharHeight();
  const wxSize button = m_noticeButton->GetSize();
  const int total = titleHeight + FromDIP(8) + detailHeight + FromDIP(20) + button.y;
  const int y = area.y + (area.height - total) / 2 + titleHeight + FromDIP(8) + detailHeight + FromDIP(20);
  m_noticeButton->Move(area.x + (area.width - button.x) / 2, y);
}
