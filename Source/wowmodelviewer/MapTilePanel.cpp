/*
 * MapTilePanel.cpp -- the Maps viewer's tile grid (MapTilePanel.h).
 */

#include "MapTilePanel.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>

#include <algorithm>
#include <cmath>

#include "MapsViewer.h"
#include "UiControls.h"
#include "UiStyle.h"

namespace
{
  const int MinCell = 3;    // a whole 64-tile grid in a 200 px pane
  const int MaxCell = 28;   // a map of a few tiles: its cells stay tile-sized, not panel-sized

  wxColour mix(const wxColour & a, const wxColour & b, double t)
  {
    return wxColour((unsigned char)(a.Red() + (b.Red() - a.Red()) * t), (unsigned char)(a.Green() + (b.Green() - a.Green()) * t),
                    (unsigned char)(a.Blue() + (b.Blue() - a.Blue()) * t));
  }

  // The colour of each tile state, from the palette's roles (light and dark alike).
  wxColour stateColour(MapsViewer::TileState s)
  {
    const UiStyle::Palette & p = UiStyle::palette();
    switch (s)
    {
      case MapsViewer::TileState::Available:    return p.accent;
      case MapsViewer::TileState::NotLocal:     return p.warning;
      case MapsViewer::TileState::NotInStorage: return mix(p.warning, p.textSecondary, 0.6);
      case MapsViewer::TileState::NoRoot:       return mix(p.textDisabled, p.controlBackground, 0.3);
      case MapsViewer::TileState::NotInWdt:     return p.controlBackground;
    }
    return p.controlBackground;
  }

  wxString tileText(int x, int y) { return wxString::Format(wxT("%d_%d"), x, y); }
  wxString qs(const QString & s) { return wxString(s.toStdWString()); }
}

// ---------------------------------------------------------------- the grid

MapTileGrid::MapTileGrid(wxWindow * parent, MapsViewer * maps)
  : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE, wxT("mapTileGrid")),
    m_maps(maps)
{
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(FromDIP(wxSize(160, 160)));
  SetName(wxT("mapTileGrid"));
  Bind(wxEVT_PAINT, &MapTileGrid::OnPaint, this);
  Bind(wxEVT_MOTION, &MapTileGrid::OnMouse, this);
  Bind(wxEVT_LEFT_DOWN, &MapTileGrid::OnMouse, this);
  Bind(wxEVT_LEFT_UP, &MapTileGrid::OnMouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &MapTileGrid::OnMouse, this);
  Bind(wxEVT_MOUSEWHEEL, &MapTileGrid::OnWheel, this);
  Bind(wxEVT_KEY_DOWN, &MapTileGrid::OnKey, this);
  Bind(wxEVT_SIZE, &MapTileGrid::OnSize, this);
}

void MapTileGrid::SetWholeGrid(bool whole)
{
  if (whole == m_whole)
    return;
  m_whole = whole;
  m_zoom = 0;
  Refresh();
}

void MapTileGrid::ZoomTo(int cell, double x, double y)
{
  m_zoom = cell <= fitCell() ? 0 : std::min(cell, FromDIP(MaxCell));
  m_cx = x;
  m_cy = y;
  Refresh();
}

int MapTileGrid::fitCell() const
{
  const int zoom = m_zoom;
  const_cast<MapTileGrid *>(this)->m_zoom = 0;
  layoutCells();
  const_cast<MapTileGrid *>(this)->m_zoom = zoom;
  const int fit = m_cell;
  layoutCells();
  return fit;
}

void MapTileGrid::Extent(int & x0, int & y0, int & x1, int & y1) const
{
  layoutCells();
  x0 = m_x0;
  y0 = m_y0;
  x1 = m_x1;
  y1 = m_y1;
}

void MapTileGrid::layoutCells() const
{
  m_x0 = m_y0 = 0;
  m_x1 = m_y1 = MapsViewer::Tiles - 1;
  int minX, minY, maxX, maxY;
  if (!m_whole && m_maps->hasGrid() && m_maps->tileExtent(minX, minY, maxX, maxY))
  {
    // The box of the tiles the WDT flags, a tile of margin round it, square-ish so a map one tile wide is not a sliver.
    m_x0 = std::max(0, minX - 1);
    m_y0 = std::max(0, minY - 1);
    m_x1 = std::min(MapsViewer::Tiles - 1, maxX + 1);
    m_y1 = std::min(MapsViewer::Tiles - 1, maxY + 1);
  }
  const wxSize size = GetClientSize();
  const int labelW = GetTextExtent(wxT("00")).x + FromDIP(6);
  const int labelH = GetTextExtent(wxT("00")).y + FromDIP(2);
  const int legendH = 2 * (GetTextExtent(wxT("Ag")).y + FromDIP(4)) + FromDIP(6);
  const int availW = std::max(0, size.x - labelW - FromDIP(4));
  const int availH = std::max(0, size.y - labelH - legendH - FromDIP(4));
  if (m_zoom > 0)
  {
    // Zoomed: cells of the size asked for, as many as fit, about the centre tile (kept inside the 64 x 64 grid).
    m_cell = m_zoom;
    const int cols = std::max(1, std::min(MapsViewer::Tiles, availW / m_cell));
    const int rows = std::max(1, std::min(MapsViewer::Tiles, availH / m_cell));
    m_x0 = std::max(0, std::min(MapsViewer::Tiles - cols, (int)std::floor(m_cx - cols / 2.0 + 0.5)));
    m_y0 = std::max(0, std::min(MapsViewer::Tiles - rows, (int)std::floor(m_cy - rows / 2.0 + 0.5)));
    m_x1 = m_x0 + cols - 1;
    m_y1 = m_y0 + rows - 1;
    m_left = labelW + std::max(0, (availW - m_cell * cols) / 2);
    m_top = labelH;
    return;
  }
  const int cols = m_x1 - m_x0 + 1, rows = m_y1 - m_y0 + 1;
  m_cell = std::max(MinCell, std::min(FromDIP(MaxCell), std::min(availW / cols, availH / rows)));
  m_left = labelW + std::max(0, (availW - m_cell * cols) / 2);
  m_top = labelH;
}

wxRect MapTileGrid::TileRect(int x, int y) const
{
  layoutCells();
  return wxRect(m_left + (x - m_x0) * m_cell, m_top + (y - m_y0) * m_cell, m_cell, m_cell);
}

bool MapTileGrid::TileAt(const wxPoint & p, int & x, int & y) const
{
  layoutCells();
  if (p.x < m_left || p.y < m_top)
    return false;
  x = m_x0 + (p.x - m_left) / m_cell;
  y = m_y0 + (p.y - m_top) / m_cell;
  return x >= m_x0 && x <= m_x1 && y >= m_y0 && y <= m_y1;
}

void MapTileGrid::OnSize(wxSizeEvent & event)
{
  Refresh();
  event.Skip();
}

void MapTileGrid::OnPaint(wxPaintEvent & WXUNUSED(event))
{
  wxAutoBufferedPaintDC dc(this);
  const UiStyle::Palette & p = UiStyle::palette();
  dc.SetBackground(wxBrush(p.panelBackground));
  dc.Clear();
  dc.SetFont(GetFont());
  layoutCells();
  const int cols = m_x1 - m_x0 + 1, rows = m_y1 - m_y0 + 1;
  const int gap = m_cell >= 6 ? 1 : 0;

  // The coordinates on the edges: x (columns, east) above, y (rows, south) on the left -- every step tiles, the step the
  // smallest of 1, 2, 4, 8, 16 whose labels do not run together.
  dc.SetTextForeground(p.textSecondary);
  const int labelStep = [&]() {
    const int need = GetTextExtent(wxT("00")).x + FromDIP(4);
    for (int s : { 1, 2, 4, 8, 16 })
      if (s * m_cell >= need)
        return s;
    return 32;
  }();
  for (int x = m_x0; x <= m_x1; x++)
    if (x % labelStep == 0)
    {
      const wxString t = wxString::Format(wxT("%d"), x);
      const wxSize e = dc.GetTextExtent(t);
      dc.DrawText(t, m_left + (x - m_x0) * m_cell + (m_cell - e.x) / 2, 0);
    }
  const int rowStep = [&]() {
    const int need = GetTextExtent(wxT("0")).y;
    for (int s : { 1, 2, 4, 8, 16 })
      if (s * m_cell >= need)
        return s;
    return 32;
  }();
  for (int y = m_y0; y <= m_y1; y++)
    if (y % rowStep == 0)
    {
      const wxString t = wxString::Format(wxT("%d"), y);
      const wxSize e = dc.GetTextExtent(t);
      dc.DrawText(t, m_left - e.x - FromDIP(4), m_top + (y - m_y0) * m_cell + (m_cell - e.y) / 2);
    }

  // The cells.
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(mix(p.controlBackground, p.border, 0.35)));
  dc.DrawRectangle(m_left - 1, m_top - 1, cols * m_cell + 1, rows * m_cell + 1);
  const bool grid = m_maps->hasGrid();
  for (int y = m_y0; y <= m_y1; y++)
    for (int x = m_x0; x <= m_x1; x++)
    {
      const MapsViewer::TileState s = grid ? m_maps->tileState(x, y) : MapsViewer::TileState::NotInWdt;
      dc.SetBrush(wxBrush(stateColour(s)));
      dc.DrawRectangle(m_left + (x - m_x0) * m_cell, m_top + (y - m_y0) * m_cell, m_cell - gap, m_cell - gap);
    }
  // The tile on screen (filled with the text colour's dot), the tile picked (a ring), the keyboard's (dotted), the mouse's.
  const auto ring = [&](int x, int y, const wxColour & c, int width, wxPenStyle style) {
    if (x < m_x0 || x > m_x1 || y < m_y0 || y > m_y1)
      return;
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(c, width, style));
    const wxRect r = TileRect(x, y);
    dc.DrawRectangle(r.x - width / 2, r.y - width / 2, r.width + width - gap, r.height + width - gap);
  };
  if (grid && m_maps->shownMap() == m_maps->mapId() && m_maps->shownTileX() >= 0)
  {
    const wxRect r = TileRect(m_maps->shownTileX(), m_maps->shownTileY());
    const int d = std::max(2, m_cell / 3);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(p.textOnAccent));
    if (m_maps->shownTileX() >= m_x0 && m_maps->shownTileX() <= m_x1 && m_maps->shownTileY() >= m_y0 && m_maps->shownTileY() <= m_y1)
      dc.DrawEllipse(r.x + (r.width - gap - d) / 2, r.y + (r.height - gap - d) / 2, d, d);
  }
  if (grid && m_maps->tileX() >= 0)
    ring(m_maps->tileX(), m_maps->tileY(), p.text, std::max(2, FromDIP(2)), wxPENSTYLE_SOLID);
  if (grid && m_keyX >= 0 && HasFocus())
    ring(m_keyX, m_keyY, p.text, 1, wxPENSTYLE_DOT);
  if (grid && m_hoverX >= 0)
    ring(m_hoverX, m_hoverY, p.borderStrong, 1, wxPENSTYLE_SOLID);

  // The legend.
  struct Key
  {
    MapsViewer::TileState state;
    const wxChar * text;
  };
  const Key keys[] = {
    { MapsViewer::TileState::Available, wxT("Available") },
    { MapsViewer::TileState::NotLocal, wxT("Not downloaded") },
    { MapsViewer::TileState::NotInStorage, wxT("Not in client files") },
    { MapsViewer::TileState::NoRoot, wxT("No root ADT") },
  };
  int lx = FromDIP(2), ly = m_top + rows * m_cell + FromDIP(8);
  const int sw = GetTextExtent(wxT("Ag")).y;
  const int width = GetClientSize().x;
  dc.SetTextForeground(p.textSecondary);
  for (const Key & k : keys)
  {
    const wxSize e = dc.GetTextExtent(k.text);
    if (lx + sw + FromDIP(4) + e.x > width && lx > FromDIP(2))
    {
      lx = FromDIP(2);
      ly += sw + FromDIP(4);
    }
    dc.SetPen(wxPen(p.border));
    dc.SetBrush(wxBrush(stateColour(k.state)));
    dc.DrawRectangle(lx, ly, sw, sw);
    dc.DrawText(k.text, lx + sw + FromDIP(4), ly);
    lx += sw + FromDIP(4) + e.x + FromDIP(10);
  }
  if (!grid)
  {
    dc.SetTextForeground(p.textSecondary);
    const wxString t = m_maps->mapId() < 0 ? wxString(_("Pick a map to see its tiles.")) : wxString(_("No tile grid yet."));
    const wxSize e = dc.GetTextExtent(t);
    dc.DrawText(t, m_left + (cols * m_cell - e.x) / 2, m_top + (rows * m_cell - e.y) / 2);
  }
}

void MapTileGrid::OnWheel(wxMouseEvent & event)
{
  if (!m_maps->hasGrid() || event.GetWheelRotation() == 0)
    return;
  int tx = -1, ty = -1;
  layoutCells();
  const bool on = TileAt(event.GetPosition(), tx, ty);
  const double ax = on ? tx + 0.5 : (m_x0 + m_x1 + 1) / 2.0, ay = on ? ty + 0.5 : (m_y0 + m_y1 + 1) / 2.0;
  const int fit = fitCell(), from = m_zoom > 0 ? m_zoom : fit;
  const int to = event.GetWheelRotation() > 0 ? std::min(FromDIP(MaxCell), std::max(from + 1, (int)(from * 1.4)))
                                              : (int)(from / 1.4);
  if (m_zoom == 0)
  {
    m_cx = (m_x0 + m_x1 + 1) / 2.0;
    m_cy = (m_y0 + m_y1 + 1) / 2.0;
  }
  // The tile under the mouse stays under it.
  const double k = (double)from / std::max(1, to);
  ZoomTo(to, ax + (m_cx - ax) * k, ay + (m_cy - ay) * k);
}

void MapTileGrid::OnMouse(wxMouseEvent & event)
{
  // A drag moves a zoomed grid; a press and release without one is a click.
  if (event.GetEventType() == wxEVT_LEFT_DOWN)
  {
    m_down = event.GetPosition();
    m_pressed = true;
    m_panned = false;
    m_downCx = m_cx;
    m_downCy = m_cy;
    SetFocus();
    if (!HasCapture())
      CaptureMouse();
    event.Skip();
    return;
  }
  if (event.GetEventType() == wxEVT_MOTION && m_pressed && event.LeftIsDown() && m_zoom > 0)
  {
    const wxPoint d = event.GetPosition() - m_down;
    if (m_panned || std::abs(d.x) + std::abs(d.y) > FromDIP(4))
    {
      m_panned = true;
      m_cx = m_downCx - (double)d.x / m_cell;
      m_cy = m_downCy - (double)d.y / m_cell;
      Refresh();
      return;
    }
  }
  int x = -1, y = -1;
  const bool on = event.GetEventType() != wxEVT_LEAVE_WINDOW && m_maps->hasGrid() && TileAt(event.GetPosition(), x, y);
  if (!on)
    x = y = -1;
  if (x != m_hoverX || y != m_hoverY)
  {
    m_hoverX = x;
    m_hoverY = y;
    Refresh();
    if (MapTilePanel * panel = dynamic_cast<MapTilePanel *>(GetParent()))
      panel->HoverChanged();
  }
  if (event.GetEventType() == wxEVT_LEFT_UP)
  {
    if (HasCapture())
      ReleaseMouse();
    const bool click = m_pressed && !m_panned;
    m_pressed = false;
    if (click && on)
    {
      m_keyX = x;
      m_keyY = y;
      m_maps->SelectTile(x, y);
    }
  }
  event.Skip();
}

void MapTileGrid::OnKey(wxKeyEvent & event)
{
  if (!m_maps->hasGrid())
  {
    event.Skip();
    return;
  }
  layoutCells();
  if (m_keyX < 0)
  {
    m_keyX = m_maps->tileX() >= 0 ? m_maps->tileX() : m_x0;
    m_keyY = m_maps->tileY() >= 0 ? m_maps->tileY() : m_y0;
  }
  int dx = 0, dy = 0;
  switch (event.GetKeyCode())
  {
    case WXK_LEFT:  dx = -1; break;
    case WXK_RIGHT: dx = 1; break;
    case WXK_UP:    dy = -1; break;
    case WXK_DOWN:  dy = 1; break;
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
    case WXK_SPACE:
      if (!event.IsAutoRepeat())   // a key held down loads once
        m_maps->SelectTile(m_keyX, m_keyY);
      return;
    default:
      event.Skip();
      return;
  }
  m_keyX = std::max(0, std::min(MapsViewer::Tiles - 1, m_keyX + dx));
  m_keyY = std::max(0, std::min(MapsViewer::Tiles - 1, m_keyY + dy));
  m_hoverX = m_keyX;
  m_hoverY = m_keyY;
  Refresh();
  if (MapTilePanel * panel = dynamic_cast<MapTilePanel *>(GetParent()))
    panel->HoverChanged();
}

// ---------------------------------------------------------------- the panel

MapTilePanel::MapTilePanel(wxWindow * parent, MapsViewer * maps)
  : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, wxT("mapTilePanel")), m_maps(maps)
{
  UiStyle::applyPanel(this);
  const int sp = FromDIP(UiStyle::S), xs = FromDIP(UiStyle::XS);
  m_title = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
  m_title->SetFont(UiStyle::font(UiStyle::Type::Section));
  m_facts = UiStyle::secondaryLabel(this, wxEmptyString);
  m_status = UiStyle::secondaryLabel(this, wxEmptyString);
  m_grid = new MapTileGrid(this, maps);
  m_wholeBox = new wxCheckBox(this, wxID_ANY, _("Show the whole 64 x 64 grid"));
  m_wholeBox->SetToolTip(_("The grid shows the box of the map's tiles; tick to show every tile of the 64 x 64 ADT grid"));
  m_hover = UiStyle::secondaryLabel(this, wxEmptyString);
  m_tile = new wxStaticText(this, wxID_ANY, wxEmptyString);
  wxStaticText * viewLabel = new wxStaticText(this, wxID_ANY, _("Display"));
  m_view = new wxChoice(this, wxID_ANY);
  m_view->SetName(wxT("mapDisplayMode"));
  for (const MapsViewer::DisplayMode & m : MapsViewer::displayModes())
    m_view->Append(wxString::FromUTF8(m.label));
  m_view->SetSelection(maps->displayMode());
  m_view->SetToolTip(_("How the terrain tile is drawn: its textures, or one of the geometry and material diagnostics. "
                       "Changing it draws the tile on screen again without reading it again."));
  m_viewNote = UiStyle::secondaryLabel(this, wxEmptyString);
  m_clear = new UiButton(this, wxID_ANY, _("Clear terrain"), UiButton::Kind::Secondary);
  m_clear->SetName(wxT("mapClearTerrain"));
  m_clear->SetToolTip(_("Take the terrain tile away from the viewport"));

  wxBoxSizer * view = new wxBoxSizer(wxHORIZONTAL);
  view->Add(viewLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, sp);
  view->Add(m_view, 1, wxALIGN_CENTER_VERTICAL);

  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
  top->Add(UiStyle::separator(this), 0, wxEXPAND | wxBOTTOM, sp);
  top->Add(m_title, 0, wxEXPAND | wxLEFT | wxRIGHT, sp);
  top->Add(m_facts, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(m_wholeBox, 0, wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(m_hover, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(m_tile, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(view, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(m_viewNote, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(m_clear, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, sp);
  SetSizer(top);

  m_wholeBox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent & e) { m_grid->SetWholeGrid(e.IsChecked()); });
  m_view->Bind(wxEVT_CHOICE, [this](wxCommandEvent & e) { m_maps->SetDisplayMode(e.GetSelection()); });
  m_clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { m_maps->ClearTerrain(); });
  Bind(wxEVT_SIZE, [this](wxSizeEvent & e) {
    e.Skip();
    if (m_refreshQueued)
      return;   // a sash dragged: one refresh after the events, not one per event
    m_refreshQueued = true;
    CallAfter([this]() {
      m_refreshQueued = false;
      Refresh2();
    });
  });
  Refresh2();
}

void MapTilePanel::setLabel(wxStaticText * label, const wxString & text)
{
  const int width = std::max(FromDIP(100), GetClientSize().x - 2 * FromDIP(UiStyle::S));
  if (label->GetLabel() != text || label->GetSize().x != width)
  {
    label->SetLabel(text);
    label->Wrap(-1);
    label->Wrap(width);
    label->Refresh();
  }
  label->Show(!text.IsEmpty());
}

void MapTilePanel::Refresh2()
{
  const int id = m_maps->mapId();
  if (id < 0)
  {
    setLabel(m_title, _("No map selected"));
    setLabel(m_facts, _("Pick a map in the list above. Search by name or MapID."));
  }
  else
  {
    const QString name = m_maps->mapName().isEmpty() ? m_maps->mapDirectory() : m_maps->mapName();
    setLabel(m_title, wxString::Format(wxT("%s  [%d]"), qs(name), id));
    setLabel(m_facts, qs(m_maps->mapDirectory() + " -- " + m_maps->wdtDescription()));
  }
  setLabel(m_status, qs(m_maps->statusLine()));
  QString tile;
  switch (m_maps->tilePhase())
  {
    case MapsViewer::TilePhase::None:
      tile = m_maps->hasGrid() && m_maps->tilesAvailable() > 0 ? QString("Click an available tile to load it.") : QString();
      break;
    case MapsViewer::TilePhase::Loading:
      tile = QString("Loading tile %1_%2 ...").arg(m_maps->tileX()).arg(m_maps->tileY());
      break;
    default:
      tile = m_maps->tileDetail();
      break;
  }
  if (m_maps->tileX() >= 0)
    tile = QString("Selected x %1, y %2.  ").arg(m_maps->tileX()).arg(m_maps->tileY()) + tile;
  setLabel(m_tile, qs(tile));
  setLabel(m_viewNote, qs(m_maps->viewDetail()));
  if (m_view->GetSelection() != m_maps->displayMode())
    m_view->SetSelection(m_maps->displayMode());
  m_clear->Enable(m_maps->shownTileX() >= 0 || m_maps->tilePhase() == MapsViewer::TilePhase::Loading);
  m_wholeBox->Enable(m_maps->hasGrid());
  HoverChanged();
  m_grid->Refresh();
  Layout();
}

void MapTilePanel::HoverChanged()
{
  const int x = m_grid->hoverX(), y = m_grid->hoverY();
  if (x < 0 || !m_maps->hasGrid())
  {
    setLabel(m_hover, m_maps->hasGrid() ? _("Point at a tile to see its coordinates; the wheel zooms, a drag moves.")
                                        : wxString());
    return;
  }
  setLabel(m_hover, wxString::Format(_("Tile x %d, y %d (%s): %s"), x, y, tileText(x, y),
                                      qs(MapsViewer::stateName(m_maps->tileState(x, y)))));
}
