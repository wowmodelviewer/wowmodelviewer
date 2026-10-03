/*
 * UiArt.cpp
 *
 * The pane chrome and the toolbars (see UiArt.h).
 */

#include "UiArt.h"

#include <algorithm>
#include <memory>

#include <wx/control.h>
#include <wx/graphics.h>

#include "UiStyle.h"

using UiStyle::palette;

namespace
{
  std::unique_ptr<wxGraphicsContext> graphicsFor(wxDC & dc)
  {
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (gc)
      gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
    return gc;
  }
}

// ======================================================================================= dock art

UiDockArt::UiDockArt(const wxWindow * frame) : m_frame(frame)
{
  // The sizes the docked layout has always had (UiStyle.h), so the viewport keeps its size; a square
  // close button the caption's height.
  SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);
  SetMetric(wxAUI_DOCKART_CAPTION_SIZE, UiStyle::dip(frame, UiStyle::CaptionHeight));
  SetMetric(wxAUI_DOCKART_SASH_SIZE, UiStyle::dip(frame, UiStyle::SashSize));
  SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 1);
  SetMetric(wxAUI_DOCKART_PANE_BUTTON_SIZE, UiStyle::dip(frame, UiStyle::CaptionHeight));
  applyPalette();
}

wxAuiDockArt * UiDockArt::Clone()
{
  return new UiDockArt(*this);
}

void UiDockArt::applyPalette()
{
  const UiStyle::Palette & p = palette();
  SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, p.appBackground);
  SetColour(wxAUI_DOCKART_SASH_COLOUR, p.appBackground);
  SetColour(wxAUI_DOCKART_BORDER_COLOUR, p.border);
  SetColour(wxAUI_DOCKART_GRIPPER_COLOUR, p.appBackground);
  SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, p.panelBackground);
  SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, p.panelBackground);
  SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, p.textSecondary);
  SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, p.panelBackground);
  SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, p.panelBackground);
  SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, p.text);
  SetFont(wxAUI_DOCKART_CAPTION_FONT, UiStyle::font(UiStyle::Type::Section));
}

void UiDockArt::UpdateColoursFromSystem()
{
  // The manager calls this when Windows' colours change: the palette's colours, not the system's as the
  // default art would take them. (Which palette is in use -- light, dark, high contrast -- the frame
  // decides: ModelViewer::ApplyTheme.)
  wxAuiDefaultDockArt::UpdateColoursFromSystem();
  applyPalette();
}

bool UiDockArt::SetActivePane(const wxString & name)
{
  if (name == m_activePane)
    return false;
  m_activePane = name;
  return true;
}

bool UiDockArt::isActive(const wxAuiPaneInfo & pane) const
{
  return (pane.state & wxAuiPaneInfo::optionActive) || (!m_activePane.IsEmpty() && pane.name == m_activePane);
}

void UiDockArt::DrawSash(wxDC & dc, wxWindow * WXUNUSED(window), int WXUNUSED(orientation), const wxRect & rect)
{
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(palette().appBackground));
  dc.DrawRectangle(rect);
}

void UiDockArt::DrawBackground(wxDC & dc, wxWindow * WXUNUSED(window), int WXUNUSED(orientation), const wxRect & rect)
{
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(palette().appBackground));
  dc.DrawRectangle(rect);
}

void UiDockArt::drawCaptionLine(wxDC & dc, const wxRect & rect, bool WXUNUSED(active)) const
{
  // The hairline between the caption and the pane's content.
  dc.SetPen(wxPen(palette().separator));
  dc.DrawLine(rect.x, rect.GetBottom(), rect.GetRight() + 1, rect.GetBottom());
}

void UiDockArt::DrawCaption(wxDC & dc, wxWindow * window, const wxString & text, const wxRect & rect,
                            wxAuiPaneInfo & pane)
{
  const UiStyle::Palette & p = palette();
  const bool active = isActive(pane);
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(p.panelBackground));
  dc.DrawRectangle(rect);
  drawCaptionLine(dc, rect, active);

  int buttons = 0;
  if (pane.HasCloseButton())
    buttons++;
  if (pane.HasPinButton())
    buttons++;
  if (pane.HasMaximizeButton())
    buttons++;
  const wxWindow * scale = window ? window : m_frame;
  const int pad = UiStyle::dip(scale, UiStyle::S);
  const int available = std::max(0, rect.width - pad - buttons * GetMetric(wxAUI_DOCKART_PANE_BUTTON_SIZE) -
                                        UiStyle::dip(scale, UiStyle::XS));
  dc.SetFont(GetFont(wxAUI_DOCKART_CAPTION_FONT));
  const wxString shown = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, available);
  const wxSize extent = dc.GetTextExtent(shown.IsEmpty() ? wxString(wxT("X")) : shown);
  dc.SetClippingRegion(rect);
  dc.SetTextForeground(active ? p.text : p.textSecondary);
  dc.DrawText(shown, rect.x + pad, rect.y + (rect.height - 1 - extent.y) / 2);
  // The pane holding the keyboard: an accent line under its title.
  if (active && !shown.IsEmpty())
  {
    const int thick = std::max(2, UiStyle::dip(scale, 2));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(p.accent));
    dc.DrawRectangle(rect.x + pad, rect.GetBottom() + 1 - thick, extent.x, thick);
  }
  dc.DestroyClippingRegion();
}

void UiDockArt::DrawPaneButton(wxDC & dc, wxWindow * window, int button, int buttonState, const wxRect & rect,
                               wxAuiPaneInfo & pane)
{
  if (button != wxAUI_BUTTON_CLOSE)
  {
    wxAuiDefaultDockArt::DrawPaneButton(dc, window, button, buttonState, rect, pane);
    return;
  }
  const UiStyle::Palette & p = palette();
  // Its own background: a hover change repaints the button alone.
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(p.panelBackground));
  dc.DrawRectangle(rect);
  dc.SetPen(wxPen(p.separator));
  dc.DrawLine(rect.x, rect.GetBottom(), rect.GetRight() + 1, rect.GetBottom());

  std::unique_ptr<wxGraphicsContext> gc = graphicsFor(dc);
  if (!gc)
    return;
  const wxWindow * scale = window ? window : m_frame;
  const bool hot = buttonState == wxAUI_BUTTON_STATE_HOVER || buttonState == wxAUI_BUTTON_STATE_PRESSED;
  const double side = std::min(rect.width, rect.height) - 2.0 * UiStyle::dip(scale, 3);
  const double x = rect.x + (rect.width - side) / 2.0, y = rect.y + (rect.height - 1 - side) / 2.0;
  if (hot)
  {
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(buttonState == wxAUI_BUTTON_STATE_PRESSED ? p.pressed : p.hover));
    gc->DrawRoundedRectangle(x, y, side, side, UiStyle::dip(scale, UiStyle::Radius));
  }
  const double cx = x + side / 2.0, cy = y + side / 2.0, arm = UiStyle::dip(scale, 4) - 0.5;
  gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(hot ? p.text : p.textSecondary)
                               .Width(scale ? scale->FromDIP(1) * 1.25 : 1.25).Cap(wxCAP_ROUND)));
  gc->StrokeLine(cx - arm, cy - arm, cx + arm, cy + arm);
  gc->StrokeLine(cx - arm, cy + arm, cx + arm, cy - arm);
}

// ==================================================================================== toolbar art

UiToolBarArt::UiToolBarArt(bool bottomLine) : m_bottomLine(bottomLine)
{
  SetFont(UiStyle::font(UiStyle::Type::Normal));
}

wxAuiToolBarArt * UiToolBarArt::Clone()
{
  return new UiToolBarArt(*this);
}

void UiToolBarArt::UpdateColoursFromSystem()
{
  wxAuiGenericToolBarArt::UpdateColoursFromSystem();
}

UiToolBarArt::Role UiToolBarArt::roleOf(const wxAuiToolBarItem & item) const
{
  const auto found = m_roles.find(item.GetId());
  if (found != m_roles.end() && found->second != Role::Auto)
    return found->second;
  switch (item.GetKind())
  {
    case wxITEM_RADIO: return Role::Segment;
    case wxITEM_CHECK: return Role::Toggle;
    default: return Role::Ghost;
  }
}

UiIcon UiToolBarArt::iconOf(const wxAuiToolBarItem & item) const
{
  const auto found = m_icons.find(item.GetId());
  return found == m_icons.end() ? UiIcon::None : found->second;
}

bool UiToolBarArt::segmentNeighbour(wxWindow * wnd, const wxAuiToolBarItem & item, int step) const
{
  const wxAuiToolBar * bar = wxDynamicCast(wnd, wxAuiToolBar);
  if (!bar)
    return false;
  const int index = bar->GetToolIndex(item.GetId());
  const wxAuiToolBarItem * other = index < 0 ? nullptr : bar->FindToolByIndex(index + step);
  return other && other->GetKind() == wxITEM_RADIO && roleOf(*other) == Role::Segment;
}

void UiToolBarArt::DrawBackground(wxDC & dc, wxWindow * wnd, const wxRect & rect)
{
  DrawPlainBackground(dc, wnd, rect);
}

void UiToolBarArt::DrawPlainBackground(wxDC & dc, wxWindow * WXUNUSED(wnd), const wxRect & rect)
{
  const UiStyle::Palette & p = palette();
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(p.panelBackground));
  dc.DrawRectangle(rect);
  if (m_bottomLine)
  {
    dc.SetPen(wxPen(p.separator));
    dc.DrawLine(rect.x, rect.GetBottom(), rect.GetRight() + 1, rect.GetBottom());
  }
}

wxSize UiToolBarArt::GetLabelSize(wxDC & dc, wxWindow * wnd, const wxAuiToolBarItem & item)
{
  dc.SetFont(UiStyle::font(UiStyle::Type::Strong));
  const int width = dc.GetTextExtent(item.GetLabel().Upper()).x;
  return wxSize(width + UiStyle::dip(wnd, UiStyle::S), UiStyle::dip(wnd, UiStyle::ToolHeight));
}

void UiToolBarArt::DrawLabel(wxDC & dc, wxWindow * WXUNUSED(wnd), const wxAuiToolBarItem & item, const wxRect & rect)
{
  // A group's name ("VIEWER", "ALPHA"): small capitals in the secondary colour, so the controls after it
  // read as the content and the label as its caption.
  dc.SetFont(UiStyle::font(UiStyle::Type::Strong));
  dc.SetTextForeground(palette().textSecondary);
  const wxString text = item.GetLabel().Upper();
  const wxSize extent = dc.GetTextExtent(text);
  dc.DrawText(text, rect.x, rect.y + (rect.height - extent.y) / 2);
}

wxSize UiToolBarArt::GetToolSize(wxDC & dc, wxWindow * wnd, const wxAuiToolBarItem & item)
{
  const Role role = roleOf(item);
  const UiIcon icon = iconOf(item);
  // A segment is measured in its selected (semibold) weight, so selecting it does not clip it.
  dc.SetFont(UiStyle::font(role == Role::Segment ? UiStyle::Type::Strong : UiStyle::Type::Normal));
  const wxString label = wxStripMenuCodes(item.GetLabel());
  int width = label.IsEmpty() ? 0 : dc.GetTextExtent(label).x;
  if (icon != UiIcon::None)
    width += UiStyle::dip(wnd, UiStyle::IconSize) + (label.IsEmpty() ? 0 : UiStyle::dip(wnd, 6));
  const int pad = UiStyle::dip(wnd, role == Role::Segment ? 12 : 10);
  // The design's height, or more when Windows' text size makes the label taller.
  const int height = std::max(UiStyle::dip(wnd, UiStyle::ToolHeight), dc.GetCharHeight() + UiStyle::dip(wnd, 8));
  return wxSize(width + 2 * pad, height);
}

void UiToolBarArt::DrawButton(wxDC & dc, wxWindow * wnd, const wxAuiToolBarItem & item, const wxRect & rect)
{
  const UiStyle::Palette & p = palette();
  const int state = item.GetState();
  const bool disabled = (state & wxAUI_BUTTON_STATE_DISABLED) != 0;
  const bool hover = !disabled && (state & wxAUI_BUTTON_STATE_HOVER) != 0;
  const bool pressed = !disabled && (state & wxAUI_BUTTON_STATE_PRESSED) != 0;
  const bool checked = (state & wxAUI_BUTTON_STATE_CHECKED) != 0;
  const Role role = roleOf(item);
  const double radius = UiStyle::dip(wnd, UiStyle::Radius);

  wxColour ink = disabled ? p.textDisabled : p.text;
  wxColour iconInk;   // the icon's colour when it is not the label's
  bool strong = false;
  {
    std::unique_ptr<wxGraphicsContext> gc = graphicsFor(dc);
    if (gc && role == Role::Segment)
    {
      // One segmented control: the group's outer corners rounded, a hairline between segments, the
      // selected segment filled with the accent.
      const bool first = !segmentNeighbour(wnd, item, -1), last = !segmentNeighbour(wnd, item, +1);
      double x = rect.x + 0.5, w = rect.width - 1.0;
      if (!first)
      {
        x -= radius + 2;
        w += radius + 2;
      }
      if (!last)
        w += radius + 2;
      wxColour fill = p.controlBackground, edge = p.border;
      if (checked)
      {
        fill = pressed ? p.accentPressed : hover ? p.accentHover : p.accent;
        edge = fill;
        ink = disabled ? p.textDisabled : p.textOnAccent;
        strong = true;
      }
      else if (pressed)
        fill = p.pressed;
      else if (hover)
        fill = p.hover;
      gc->Clip(rect.x, rect.y, rect.width, rect.height);
      gc->SetBrush(wxBrush(fill));
      gc->SetPen(wxPen(edge));
      gc->DrawRoundedRectangle(x, rect.y + 0.5, w, rect.height - 1.0, radius);
      if (!first && !checked)
      {
        gc->SetPen(wxPen(p.border));
        gc->StrokeLine(rect.x + 0.5, rect.y + 0.5, rect.x + 0.5, rect.y + rect.height - 0.5);
      }
      gc->ResetClip();
    }
    else if (gc)
    {
      // A ghost button shows a fill only under the mouse. A toggle that is on (its panel shown) has a
      // fill, an outline and its icon in the accent, so on and off differ by more than a shade -- and on
      // under the mouse still differs from off under the mouse.
      const bool on = role == Role::Toggle && checked;
      wxColour fill, edge;
      if (pressed)
        fill = p.pressed;
      else if (hover)
        fill = on ? p.pressed : p.hover;
      else if (on)
        fill = p.checked;
      if (on && !disabled)
        edge = p.borderStrong;
      if (fill.IsOk() || edge.IsOk())
      {
        gc->SetPen(edge.IsOk() ? wxPen(edge) : *wxTRANSPARENT_PEN);
        gc->SetBrush(fill.IsOk() ? wxBrush(fill) : *wxTRANSPARENT_BRUSH);
        gc->DrawRoundedRectangle(rect.x + 0.5, rect.y + 0.5, rect.width - 1.0, rect.height - 1.0, radius);
      }
      if (role == Role::Toggle && !checked && !disabled)
        ink = p.textSecondary;
      if (on && !disabled)
        iconInk = p.accent;
    }
  }

  // The content: icon, then label, centred.
  const UiIcon icon = iconOf(item);
  const wxString label = wxStripMenuCodes(item.GetLabel());
  dc.SetFont(UiStyle::font(strong ? UiStyle::Type::Strong : UiStyle::Type::Normal));
  const wxSize text = label.IsEmpty() ? wxSize(0, 0) : dc.GetTextExtent(label);
  const int iconSize = icon == UiIcon::None ? 0 : UiStyle::dip(wnd, UiStyle::IconSize);
  const int gap = (iconSize && text.x) ? UiStyle::dip(wnd, 6) : 0;
  int x = rect.x + (rect.width - (iconSize + gap + text.x)) / 2;
  if (iconSize)
  {
    const wxBitmap bmp = UiIcons::bitmap(icon, iconInk.IsOk() ? iconInk : ink, wnd, UiStyle::IconSize);
    if (bmp.IsOk())
      dc.DrawBitmap(bmp, x, rect.y + (rect.height - bmp.GetHeight()) / 2, true);
    x += iconSize + gap;
  }
  if (text.x)
  {
    dc.SetTextForeground(ink);
    dc.DrawText(label, x, rect.y + (rect.height - text.y) / 2);
  }
}

void UiToolBarArt::DrawSeparator(wxDC & dc, wxWindow * wnd, const wxRect & rect)
{
  const int x = rect.x + rect.width / 2;
  const int inset = UiStyle::dip(wnd, 10);
  dc.SetPen(wxPen(palette().separator));
  dc.DrawLine(x, rect.y + inset, x, rect.GetBottom() - inset + 1);
}
