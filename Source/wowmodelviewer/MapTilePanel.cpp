/*
 * MapTilePanel.cpp -- the Maps viewer's tile grid and controls (MapTilePanel.h).
 */

#include "MapTilePanel.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/textctrl.h>

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

#include "MapsViewer.h"
#include "UiControls.h"
#include "UiStyle.h"
#include "logger/Logger.h"

namespace
{
  const int MinCell = 3;     // a whole 64-tile grid in a 200 px pane
  const int MaxFitCell = 96; // a map of a few tiles: fitted to tile-sized cells, not to the whole view
  const int MaxCell = 160;   // the wheel's closest: a minimap picture five times its thumbnail's size

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

  // A number typed in a target field: '.' or ',' for the decimals, whatever the system's locale says.
  bool parseNumber(const wxString & text, double & v)
  {
    wxString t = text;
    t.Trim(true).Trim(false);
    t.Replace(wxT(","), wxT("."));
    return !t.IsEmpty() && t.ToCDouble(&v) && std::isfinite(v);
  }
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
  Bind(wxEVT_LEFT_DCLICK, &MapTileGrid::OnMouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &MapTileGrid::OnMouse, this);
  Bind(wxEVT_MOUSEWHEEL, &MapTileGrid::OnWheel, this);
  Bind(wxEVT_KEY_DOWN, &MapTileGrid::OnKey, this);
  Bind(wxEVT_SIZE, &MapTileGrid::OnSize, this);
  m_repaintTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { Refresh(false); });
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
    // The box of the tiles the WDT flags, a tile of margin round it.
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
  m_cell = std::max(MinCell, std::min(FromDIP(MaxFitCell), std::min(availW / cols, availH / rows)));
  m_left = labelW + std::max(0, (availW - m_cell * cols) / 2);
  m_top = labelH;
}

wxRect MapTileGrid::TileRect(int x, int y) const
{
  layoutCells();
  return wxRect(m_left + (x - m_x0) * m_cell, m_top + (y - m_y0) * m_cell, m_cell, m_cell);
}

bool MapTileGrid::PointAt(const wxPoint & p, int & x, int & y, double & fx, double & fy) const
{
  layoutCells();
  if (p.x < m_left || p.y < m_top || m_cell <= 0)
    return false;
  const double cx = (p.x - m_left + 0.5) / (double)m_cell, cy = (p.y - m_top + 0.5) / (double)m_cell;
  x = m_x0 + (int)std::floor(cx);
  y = m_y0 + (int)std::floor(cy);
  fx = cx - std::floor(cx);
  fy = cy - std::floor(cy);
  return x >= m_x0 && x <= m_x1 && y >= m_y0 && y <= m_y1;
}

bool MapTileGrid::TileAt(const wxPoint & p, int & x, int & y) const
{
  double fx, fy;
  return PointAt(p, x, y, fx, fy);
}

void MapTileGrid::OnSize(wxSizeEvent & event)
{
  Refresh();
  event.Skip();
}

void MapTileGrid::ImagesChanged()
{
  // Drawn into the mosaic at the next paint: at most one every RepaintMs while pictures come (a paint for each would keep
  // the window busy painting).
  if (!m_repaintTimer.IsRunning())
    m_repaintTimer.StartOnce(RepaintMs);
}

void MapTileGrid::updateMosaic()
{
  if (!m_maps->hasGrid())
  {
    // No grid: no mosaic, and no tile counts as drawn in one (a later map must not inherit this one's).
    if (m_mosaic.IsOk())
      m_mosaic = wxNullBitmap;
    m_inMosaic.clear();
    m_mosaicMap = -2;
    return;
  }
  const int side = MapsViewer::ThumbSide;
  if (m_mosaicMap != m_maps->mapId() || m_mosaicInspection != m_maps->inspectionSerial() || !m_mosaic.IsOk())
  {
    // A map (or a new inspection of it): a new mosaic over its box, empty.
    int minX, minY, maxX, maxY;
    m_inMosaic.assign(MapsViewer::Tiles * MapsViewer::Tiles, false);
    if (!m_maps->tileExtent(minX, minY, maxX, maxY))
    {
      m_mosaic = wxNullBitmap;
      m_mosaicMap = m_maps->mapId();
      m_mosaicInspection = m_maps->inspectionSerial();
      m_mx1 = m_mx0 - 1;
      return;
    }
    m_mx0 = minX;
    m_my0 = minY;
    m_mx1 = maxX;
    m_my1 = maxY;
    m_mosaic = wxBitmap((m_mx1 - m_mx0 + 1) * side, (m_my1 - m_my0 + 1) * side, 24);
    {
      wxMemoryDC dc(m_mosaic);
      dc.SetBackground(wxBrush(UiStyle::palette().controlBackground));
      dc.Clear();
    }
    m_inMosaic.assign(MapsViewer::Tiles * MapsViewer::Tiles, false);
    m_mosaicMap = m_maps->mapId();
    m_mosaicInspection = m_maps->inspectionSerial();
    m_mosaicSerial = -1;
  }
  if (m_mosaicSerial == m_maps->imagesSerial() || !m_mosaic.IsOk())
    return;
  // Pictures that came while the overhead map was not on screen (the world shown) are drawn now, MosaicPaintBudgetMs at
  // most per paint: the rest are drawn at the next repaint, so Back to map is never one long paint.
  QElapsedTimer clock;
  clock.start();
  bool more = false;
  int drawn = 0;
  wxMemoryDC dc(m_mosaic);
  wxImage image(side, side, false);
  for (int y = m_my0; y <= m_my1 && !more; y++)
    for (int x = m_mx0; x <= m_mx1; x++)
    {
      const int i = y * MapsViewer::Tiles + x;
      if (m_inMosaic[i])
        continue;
      if (clock.elapsed() >= MosaicPaintBudgetMs)
      {
        more = true;
        break;
      }
      const QByteArray & t = m_maps->thumbnail(x, y);
      if (t.size() != side * side * 4)
        continue;
      unsigned char * d = image.GetData();
      const unsigned char * s = reinterpret_cast<const unsigned char *>(t.constData());
      for (int k = 0; k < side * side; k++)
      {
        d[3 * k] = s[4 * k];
        d[3 * k + 1] = s[4 * k + 1];
        d[3 * k + 2] = s[4 * k + 2];
      }
      dc.DrawBitmap(wxBitmap(image, 24), (x - m_mx0) * side, (y - m_my0) * side, false);
      m_inMosaic[i] = true;
      drawn++;
    }
  if (more)
  {
    LOG_INFO << "[maps] the overhead map drew" << drawn << "picture(s) in" << clock.elapsed() << "ms, its paint budget; the rest"
             << "at the next repaint";
    if (!m_repaintTimer.IsRunning())
      m_repaintTimer.StartOnce(RepaintMs);
    return;   // (the serial stays behind: the next paint goes on)
  }
  m_mosaicSerial = m_maps->imagesSerial();
}

// ---------------------------------------------------------------- a WMO-only map's footprint

bool MapTileGrid::footprintFit(double & scale, double & left, double & top) const
{
  if (!m_maps->hasFootprint())
    return false;
  const double * b = m_maps->footprintRootBox();
  // North up (x grows up the window), east to the right (y, west, grows to the left); a margin round the box, room for
  // the legend under it.
  const wxSize size = GetClientSize();
  const int margin = FromDIP(16);
  const int legendH = 2 * (GetTextExtent(wxT("Ag")).y + FromDIP(4)) + FromDIP(6);
  const double spanX = std::max(1e-3, b[3] - b[0]), spanY = std::max(1e-3, b[4] - b[1]);
  const double availW = std::max(1, size.x - 2 * margin), availH = std::max(1, size.y - 2 * margin - legendH);
  scale = std::min(availW / spanY, availH / spanX);
  left = margin + (availW - spanY * scale) / 2.0;
  top = margin + (availH - spanX * scale) / 2.0;
  return scale > 0.0 && std::isfinite(scale);
}

bool MapTileGrid::FootprintPointAt(const wxPoint & p, double & x, double & y) const
{
  double scale, left, top;
  if (!footprintFit(scale, left, top))
    return false;
  const double * b = m_maps->footprintRootBox();
  y = b[4] - (p.x + 0.5 - left) / scale;
  x = b[3] - (p.y + 0.5 - top) / scale;
  return x >= b[0] && x <= b[3] && y >= b[1] && y <= b[4];
}

wxPoint MapTileGrid::FootprintPoint(double x, double y) const
{
  double scale, left, top;
  if (!footprintFit(scale, left, top))
    return wxPoint(-1, -1);
  const double * b = m_maps->footprintRootBox();
  return wxPoint((int)std::floor(left + (b[4] - y) * scale), (int)std::floor(top + (b[3] - x) * scale));
}

void MapTileGrid::paintFootprint(wxDC & dc)
{
  const UiStyle::Palette & p = UiStyle::palette();
  const wxSize size = GetClientSize();
  double scale, left, top;
  if (!footprintFit(scale, left, top))
  {
    // Not known yet, or not to be had: why, in the middle.
    QString title, detail;
    m_maps->viewportNotice(title, detail);
    if (title.isEmpty())
      title = "The world model";
    if (detail.isEmpty())
      detail = m_maps->footprintDetail();
    dc.SetFont(UiStyle::font(UiStyle::Type::Section));
    dc.SetTextForeground(p.text);
    const wxSize te = dc.GetTextExtent(qs(title));
    dc.DrawText(qs(title), (size.x - te.x) / 2, size.y / 2 - te.y - FromDIP(4));
    dc.SetFont(GetFont());
    dc.SetTextForeground(p.textSecondary);
    const wxSize de = dc.GetTextExtent(qs(detail));
    dc.DrawText(qs(detail), std::max(FromDIP(4), (size.x - de.x) / 2), size.y / 2 + FromDIP(4));
    return;
  }
  const double * b = m_maps->footprintRootBox();
  const auto px = [&](double x, double y) {
    return wxPoint((int)std::floor(left + (b[4] - y) * scale), (int)std::floor(top + (b[3] - x) * scale));
  };
  // The world model's placed box.
  const wxPoint nw = px(b[3], b[4]), se = px(b[0], b[1]);
  dc.SetPen(wxPen(mix(p.panelBackground, p.text, 0.45), 1, wxPENSTYLE_SHORT_DASH));
  dc.SetBrush(wxBrush(mix(p.panelBackground, p.controlBackground, 0.6)));
  dc.DrawRectangle(nw.x, nw.y, std::max(1, se.x - nw.x), std::max(1, se.y - nw.y));
  // The groups from above, the lower first (a higher group's box is drawn over the ones under it).
  const std::vector<float> & boxes = m_maps->footprintGroupBoxes();
  const std::vector<quint32> & flags = m_maps->footprintGroupFlags();
  const int groups = (int)flags.size();
  std::vector<int> order;
  for (int g = 0; g < groups && (size_t)(6 * g + 5) < boxes.size(); g++)
    order.push_back(g);
  std::sort(order.begin(), order.end(), [&boxes](int a, int c) { return boxes[6 * a + 5] < boxes[6 * c + 5]; });
  const wxColour interiorFill = mix(p.controlBackground, p.accent, 0.35), interiorLine = p.accent;
  const wxColour exteriorFill = mix(p.controlBackground, p.textSecondary, 0.25), exteriorLine = mix(p.panelBackground, p.text, 0.55);
  for (int g : order)
  {
    const float * v = &boxes[6 * g];
    const bool interior = (flags[g] & 0x2000) != 0;
    const wxPoint a = px(v[3], v[4]), c = px(v[0], v[1]);
    dc.SetPen(wxPen(interior ? interiorLine : exteriorLine));
    dc.SetBrush(wxBrush(interior ? interiorFill : exteriorFill));
    dc.DrawRectangle(a.x, a.y, std::max(1, c.x - a.x), std::max(1, c.y - a.y));
  }
  // North, and a scale bar of a round length about a fifth of the box.
  dc.SetTextForeground(p.textSecondary);
  dc.SetPen(wxPen(p.textSecondary, std::max(1, FromDIP(2))));
  const int nx = FromDIP(14), ny = FromDIP(8);
  dc.DrawLine(nx, ny + FromDIP(18), nx, ny);
  dc.DrawLine(nx, ny, nx - FromDIP(4), ny + FromDIP(6));
  dc.DrawLine(nx, ny, nx + FromDIP(4), ny + FromDIP(6));
  dc.DrawText(wxT("N"), nx + FromDIP(6), ny);
  const double want = std::max(1.0, (b[4] - b[1]) / 5.0);
  double bar = 1.0;
  for (double step : { 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0 })
    if (step <= want)
      bar = step;
  const int barPx = (int)std::lround(bar * scale);
  const int bx = size.x - barPx - FromDIP(16), by = FromDIP(12);
  dc.DrawLine(bx, by, bx + barPx, by);
  dc.DrawLine(bx, by - FromDIP(3), bx, by + FromDIP(3));
  dc.DrawLine(bx + barPx, by - FromDIP(3), bx + barPx, by + FromDIP(3));
  const wxString barText = wxString::Format(wxT("%g yd"), bar);
  dc.DrawText(barText, bx + (barPx - dc.GetTextExtent(barText).x) / 2, by + FromDIP(4));
  // The target (a marker) and the camera after a flight (a diamond).
  if (m_maps->hasTarget())
  {
    const wxPoint pt = px(m_maps->targetX(), m_maps->targetY());
    const int r = std::max(4, FromDIP(6));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(p.panelBackground, std::max(3, FromDIP(3))));
    dc.DrawCircle(pt, r);
    dc.SetPen(wxPen(p.accent, std::max(2, FromDIP(2))));
    dc.DrawCircle(pt, r);
    dc.DrawLine(pt.x - r - FromDIP(4), pt.y, pt.x - r + FromDIP(2), pt.y);
    dc.DrawLine(pt.x + r - FromDIP(2), pt.y, pt.x + r + FromDIP(4), pt.y);
    dc.DrawLine(pt.x, pt.y - r - FromDIP(4), pt.x, pt.y - r + FromDIP(2));
    dc.DrawLine(pt.x, pt.y + r - FromDIP(2), pt.x, pt.y + r + FromDIP(4));
  }
  if (m_maps->cameraPlaced() && m_maps->worldModelShown())
  {
    const wxPoint pt = px(m_maps->cameraX(), m_maps->cameraY());
    const int r = std::max(3, FromDIP(5));
    const wxPoint diamond[4] = { wxPoint(pt.x, pt.y - r), wxPoint(pt.x + r, pt.y), wxPoint(pt.x, pt.y + r), wxPoint(pt.x - r, pt.y) };
    dc.SetPen(wxPen(p.panelBackground));
    dc.SetBrush(wxBrush(p.warning));
    dc.DrawPolygon(4, diamond);
  }
  // The legend.
  int lx = FromDIP(2), ly = se.y + FromDIP(10);
  const int sw = GetTextExtent(wxT("Ag")).y;
  const struct
  {
    wxColour fill, line;
    wxString text;
  } keys[] = {
    { interiorFill, interiorLine, wxString::Format(_("Interior group (%d)"), m_maps->footprintInteriorGroups()) },
    { exteriorFill, exteriorLine, wxString::Format(_("Exterior group (%d)"), groups - m_maps->footprintInteriorGroups()) },
  };
  for (const auto & k : keys)
  {
    const wxSize e = dc.GetTextExtent(k.text);
    dc.SetPen(wxPen(k.line));
    dc.SetBrush(wxBrush(k.fill));
    dc.DrawRectangle(lx, ly, sw, sw);
    dc.DrawText(k.text, lx + sw + FromDIP(4), ly);
    lx += sw + FromDIP(4) + e.x + FromDIP(10);
  }
  const wxString extent = wxString::Format(_("X %.1f to %.1f, Y %.1f to %.1f, Z %.1f to %.1f"), b[0], b[3], b[1], b[4], b[2], b[5]);
  dc.DrawText(extent, FromDIP(2), ly + sw + FromDIP(4));
}

void MapTileGrid::footprintMouse(wxMouseEvent & event)
{
  // A click sets the target at the point clicked inside the world model's box; a double-click also enters the world.
  double x = 0.0, y = 0.0;
  const bool on = event.GetEventType() != wxEVT_LEAVE_WINDOW && FootprintPointAt(event.GetPosition(), x, y);
  if (on != m_hoverWorld || (on && (x != m_hoverWX || y != m_hoverWY)))
  {
    m_hoverWorld = on;
    m_hoverWX = x;
    m_hoverWY = y;
    if (onHover)
      onHover();
  }
  if (event.GetEventType() == wxEVT_LEFT_DOWN)
  {
    SetFocus();
    event.Skip();
    return;
  }
  if (event.GetEventType() == wxEVT_LEFT_DCLICK)
  {
    if (on && m_maps->SetTarget(x, y, false, 0.0))
      m_maps->EnterWorld();
    return;
  }
  if (event.GetEventType() == wxEVT_LEFT_UP && on)
    m_maps->SetTarget(x, y, false, 0.0);
  event.Skip();
}

void MapTileGrid::footprintKey(wxKeyEvent & event)
{
  // The arrows move the target a fortieth of the world model's box (from its middle when there is none); Enter enters.
  const double * b = m_maps->footprintRootBox();
  double x = m_maps->hasTarget() ? m_maps->targetX() : (b[0] + b[3]) / 2.0;
  double y = m_maps->hasTarget() ? m_maps->targetY() : (b[1] + b[4]) / 2.0;
  const double step = std::max(0.5, std::max(b[3] - b[0], b[4] - b[1]) / 40.0);
  switch (event.GetKeyCode())
  {
    case WXK_LEFT:  y += step; break;   // west
    case WXK_RIGHT: y -= step; break;   // east
    case WXK_UP:    x += step; break;   // north
    case WXK_DOWN:  x -= step; break;   // south
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
    case WXK_SPACE:
      if (!event.IsAutoRepeat())
        m_maps->EnterWorld();
      return;
    default:
      event.Skip();
      return;
  }
  if (!m_maps->hasTarget())
  {
    x = (b[0] + b[3]) / 2.0;
    y = (b[1] + b[4]) / 2.0;
  }
  m_maps->SetTarget(std::min(std::max(x, b[0]), b[3]), std::min(std::max(y, b[1]), b[4]), false, 0.0);
}

void MapTileGrid::OnPaint(wxPaintEvent & WXUNUSED(event))
{
  wxAutoBufferedPaintDC dc(this);
  const UiStyle::Palette & p = UiStyle::palette();
  dc.SetBackground(wxBrush(p.panelBackground));
  dc.Clear();
  dc.SetFont(GetFont());
  const wxSize size = GetClientSize();

  // A WMO-only map: its world model's footprint, not the tile grid.
  if (m_maps->worldModelMap())
  {
    paintFootprint(dc);
    return;
  }

  // No grid: why, in the middle (the viewport's notice words).
  if (!m_maps->hasGrid())
  {
    QString title, detail;
    m_maps->viewportNotice(title, detail);
    if (title.isEmpty())
      title = "No map selected";
    dc.SetFont(UiStyle::font(UiStyle::Type::Section));
    dc.SetTextForeground(p.text);
    const wxSize te = dc.GetTextExtent(qs(title));
    dc.DrawText(qs(title), (size.x - te.x) / 2, size.y / 2 - te.y - FromDIP(4));
    dc.SetFont(GetFont());
    dc.SetTextForeground(p.textSecondary);
    const wxSize de = dc.GetTextExtent(qs(detail));
    dc.DrawText(qs(detail), std::max(FromDIP(4), (size.x - de.x) / 2), size.y / 2 + FromDIP(4));
    return;
  }

  updateMosaic();
  layoutCells();
  const int cols = m_x1 - m_x0 + 1, rows = m_y1 - m_y0 + 1;
  const int side = MapsViewer::ThumbSide;

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

  // The cells: the minimap pictures (one stretched copy of the mosaic's part in view), and the state colour for a tile
  // without a picture.
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(mix(p.controlBackground, p.border, 0.35)));
  dc.DrawRectangle(m_left - 1, m_top - 1, cols * m_cell + 1, rows * m_cell + 1);
  const int gap = m_cell >= 6 ? 1 : 0;
  const int vx0 = std::max(m_x0, m_mx0), vy0 = std::max(m_y0, m_my0), vx1 = std::min(m_x1, m_mx1), vy1 = std::min(m_y1, m_my1);
  bool anyPicture = false;
  for (int y = m_y0; y <= m_y1; y++)
    for (int x = m_x0; x <= m_x1; x++)
    {
      const MapsViewer::TileState s = m_maps->tileState(x, y);
      const bool picture = !m_inMosaic.empty() && m_inMosaic[y * MapsViewer::Tiles + x];
      anyPicture = anyPicture || picture;
      if (picture)
        continue;
      dc.SetBrush(wxBrush(stateColour(s)));
      dc.DrawRectangle(m_left + (x - m_x0) * m_cell, m_top + (y - m_y0) * m_cell, m_cell - gap, m_cell - gap);
    }
  if (anyPicture && m_mosaic.IsOk() && vx0 <= vx1 && vy0 <= vy1)
  {
    wxMemoryDC src(m_mosaic);
    // Each tile with a picture, from the mosaic: rows of contiguous pictures are copied as one stretch.
    for (int y = vy0; y <= vy1; y++)
    {
      int x = vx0;
      while (x <= vx1)
      {
        if (!m_inMosaic[y * MapsViewer::Tiles + x])
        {
          x++;
          continue;
        }
        int run = x;
        while (run + 1 <= vx1 && m_inMosaic[y * MapsViewer::Tiles + run + 1])
          run++;
        dc.StretchBlit(m_left + (x - m_x0) * m_cell, m_top + (y - m_y0) * m_cell, (run - x + 1) * m_cell, m_cell, &src,
                       (x - m_mx0) * side, (y - m_my0) * side, (run - x + 1) * side, side);
        x = run + 1;
      }
    }
    // A tile with a picture that cannot be loaded is hatched over it.
    dc.SetPen(*wxTRANSPARENT_PEN);
    for (int y = vy0; y <= vy1; y++)
      for (int x = vx0; x <= vx1; x++)
      {
        const MapsViewer::TileState s = m_maps->tileState(x, y);
        if (!m_inMosaic[y * MapsViewer::Tiles + x] || s == MapsViewer::TileState::Available)
          continue;
        dc.SetBrush(wxBrush(stateColour(s), wxBRUSHSTYLE_BDIAGONAL_HATCH));
        dc.DrawRectangle(m_left + (x - m_x0) * m_cell, m_top + (y - m_y0) * m_cell, m_cell, m_cell);
      }
    // Faint tile borders over the pictures.
    if (m_cell >= 8)
    {
      dc.SetPen(wxPen(mix(p.panelBackground, p.text, 0.25)));
      for (int x = m_x0; x <= m_x1 + 1; x++)
        dc.DrawLine(m_left + (x - m_x0) * m_cell, m_top, m_left + (x - m_x0) * m_cell, m_top + rows * m_cell);
      for (int y = m_y0; y <= m_y1 + 1; y++)
        dc.DrawLine(m_left, m_top + (y - m_y0) * m_cell, m_left + cols * m_cell, m_top + (y - m_y0) * m_cell);
    }
  }

  // The tile on screen (a dot), the target's tile (a ring) and point (a marker), the camera after a flight (a diamond),
  // the mouse's tile.
  const auto ring = [&](int x, int y, const wxColour & c, int width, wxPenStyle style) {
    if (x < m_x0 || x > m_x1 || y < m_y0 || y > m_y1)
      return;
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(c, width, style));
    const wxRect r = TileRect(x, y);
    dc.DrawRectangle(r.x - width / 2, r.y - width / 2, r.width + width - gap, r.height + width - gap);
  };
  // A world point's position on the grid.
  const auto at = [&](double wx, double wy, wxPoint & pt) {
    const double col = 32.0 - wy / MapsViewer::TileSize, row = 32.0 - wx / MapsViewer::TileSize;
    if (col < m_x0 || col > m_x1 + 1 || row < m_y0 || row > m_y1 + 1)
      return false;
    pt = wxPoint(m_left + (int)std::floor((col - m_x0) * m_cell), m_top + (int)std::floor((row - m_y0) * m_cell));
    return true;
  };
  if (m_maps->shownMap() == m_maps->mapId() && m_maps->shownTileX() >= 0 && m_maps->shownTileX() >= m_x0 &&
      m_maps->shownTileX() <= m_x1 && m_maps->shownTileY() >= m_y0 && m_maps->shownTileY() <= m_y1)
  {
    const wxRect r = TileRect(m_maps->shownTileX(), m_maps->shownTileY());
    const int d = std::max(3, m_cell / 4);
    dc.SetPen(wxPen(p.panelBackground));
    dc.SetBrush(wxBrush(p.text));
    dc.DrawEllipse(r.x + FromDIP(3), r.y + FromDIP(3), d, d);
  }
  if (m_maps->tileX() >= 0)
    ring(m_maps->tileX(), m_maps->tileY(), p.text, std::max(2, FromDIP(2)), wxPENSTYLE_SOLID);
  wxPoint pt;
  if (m_maps->hasTarget() && at(m_maps->targetX(), m_maps->targetY(), pt))
  {
    const int r = std::max(4, FromDIP(6));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(p.panelBackground, std::max(3, FromDIP(3))));
    dc.DrawCircle(pt, r);
    dc.SetPen(wxPen(p.accent, std::max(2, FromDIP(2))));
    dc.DrawCircle(pt, r);
    dc.DrawLine(pt.x - r - FromDIP(4), pt.y, pt.x - r + FromDIP(2), pt.y);
    dc.DrawLine(pt.x + r - FromDIP(2), pt.y, pt.x + r + FromDIP(4), pt.y);
    dc.DrawLine(pt.x, pt.y - r - FromDIP(4), pt.x, pt.y - r + FromDIP(2));
    dc.DrawLine(pt.x, pt.y + r - FromDIP(2), pt.x, pt.y + r + FromDIP(4));
  }
  if (m_maps->cameraPlaced() && at(m_maps->cameraX(), m_maps->cameraY(), pt))
  {
    const int r = std::max(3, FromDIP(5));
    const wxPoint diamond[4] = { wxPoint(pt.x, pt.y - r), wxPoint(pt.x + r, pt.y), wxPoint(pt.x, pt.y + r), wxPoint(pt.x - r, pt.y) };
    dc.SetPen(wxPen(p.panelBackground));
    dc.SetBrush(wxBrush(p.warning));
    dc.DrawPolygon(4, diamond);
  }
  if (m_hoverX >= 0)
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
  dc.SetTextForeground(p.textSecondary);
  for (const Key & k : keys)
  {
    const wxSize e = dc.GetTextExtent(k.text);
    if (lx + sw + FromDIP(4) + e.x > size.x && lx > FromDIP(2))
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
  if (m_maps->minimapsWanted() > 0)
  {
    const wxString t = m_maps->minimapsLoading()
                         ? wxString::Format(_("Minimap pictures: %d of %d"), m_maps->minimapsLoaded(), m_maps->minimapsWanted())
                         : wxString::Format(_("Minimap pictures: %d"), m_maps->minimapsLoaded());
    const wxSize e = dc.GetTextExtent(t);
    if (lx + e.x > size.x && lx > FromDIP(2))
    {
      lx = FromDIP(2);
      ly += sw + FromDIP(4);
    }
    dc.DrawText(t, lx, ly);
  }
}

void MapTileGrid::OnWheel(wxMouseEvent & event)
{
  if (!m_maps->hasGrid() || m_maps->worldModelMap() || event.GetWheelRotation() == 0)
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

void MapTileGrid::hoverTo(int x, int y)
{
  if (x == m_hoverX && y == m_hoverY)
    return;
  m_hoverX = x;
  m_hoverY = y;
  Refresh(false);
  if (onHover)
    onHover();
}

void MapTileGrid::OnMouse(wxMouseEvent & event)
{
  if (m_maps->worldModelMap())
  {
    footprintMouse(event);
    return;
  }
  m_hoverWorld = false;
  // A drag moves a zoomed grid; a press and release without one is a click (the target); a double-click enters the world.
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
  double fx = 0.5, fy = 0.5;
  const bool on = event.GetEventType() != wxEVT_LEAVE_WINDOW && m_maps->hasGrid() && PointAt(event.GetPosition(), x, y, fx, fy);
  hoverTo(on ? x : -1, on ? y : -1);
  if (event.GetEventType() == wxEVT_LEFT_DCLICK)
  {
    if (on)
    {
      m_maps->SetTargetAt(x, y, fx, fy);
      m_maps->EnterWorld();
    }
    return;
  }
  if (event.GetEventType() == wxEVT_LEFT_UP)
  {
    if (HasCapture())
      ReleaseMouse();
    const bool click = m_pressed && !m_panned;
    m_pressed = false;
    if (click && on)
      m_maps->SetTargetAt(x, y, fx, fy);
  }
  event.Skip();
}

void MapTileGrid::OnKey(wxKeyEvent & event)
{
  if (m_maps->hasFootprint())
  {
    footprintKey(event);
    return;
  }
  if (!m_maps->hasGrid() || m_maps->worldModelMap())
  {
    event.Skip();
    return;
  }
  layoutCells();
  int x = m_maps->tileX() >= 0 ? m_maps->tileX() : (m_x0 + m_x1) / 2, y = m_maps->tileY() >= 0 ? m_maps->tileY() : (m_y0 + m_y1) / 2;
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
      if (!event.IsAutoRepeat())   // a key held down enters once
      {
        if (!m_maps->hasTarget())
          m_maps->SelectTile(x, y);
        m_maps->EnterWorld();
      }
      return;
    default:
      event.Skip();
      return;
  }
  if (m_maps->tileX() >= 0)
  {
    x = std::max(0, std::min(MapsViewer::Tiles - 1, x + dx));
    y = std::max(0, std::min(MapsViewer::Tiles - 1, y + dy));
  }
  m_maps->SelectTile(x, y);
  hoverTo(x, y);
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

  wxStaticText * targetLabel = new wxStaticText(this, wxID_ANY, _("Target"));
  targetLabel->SetFont(UiStyle::font(UiStyle::Type::Strong));
  const auto field = [this](const wxString & name, const wxString & tip) {
    wxTextCtrl * t = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(56, -1)), wxTE_PROCESS_ENTER);
    t->SetName(name);
    t->SetToolTip(tip);
    return t;
  };
  m_tx = field(wxT("mapTargetX"), _("World X (yards, north positive), as the game's coordinates give it; X and Y "
                                     "both empty: no target"));
  m_ty = field(wxT("mapTargetY"), _("World Y (yards, west positive); X and Y both empty: no target"));
  m_tz = field(wxT("mapTargetZ"), _("Height (yards); empty: on the ground"));
  m_tz->SetHint(_("ground"));
  m_target = UiStyle::secondaryLabel(this, wxEmptyString);
  m_enter = new UiButton(this, wxID_ANY, _("Enter world"), UiButton::Kind::Primary);
  m_enter->SetName(wxT("mapEnterWorld"));
  m_enter->SetToolTip(_("Load the world at the target and put the free camera there"));
  m_back = new UiButton(this, wxID_ANY, _("Back to map"), UiButton::Kind::Secondary);
  m_back->SetName(wxT("mapBackToMap"));
  m_back->SetToolTip(_("Show the overhead map again; the world stays loaded behind it"));
  m_tile = new wxStaticText(this, wxID_ANY, wxEmptyString);
  wxStaticText * cameraLabel = new wxStaticText(this, wxID_ANY, _("Camera"));
  m_camera = new wxChoice(this, wxID_ANY);
  m_camera->SetName(wxT("mapCamera"));
  m_camera->Append(_("Free camera"));
  m_camera->Append(_("Tile overview"));
  m_camera->SetSelection(0);
  m_camera->SetToolTip(_("The free camera flies from the target; the overview shows the whole tile from above"));
  m_cameraNote = UiStyle::secondaryLabel(this, wxEmptyString);
  wxStaticText * viewLabel = new wxStaticText(this, wxID_ANY, _("Display"));
  m_view = new wxChoice(this, wxID_ANY);
  m_view->SetName(wxT("mapDisplayMode"));
  for (const MapsViewer::DisplayMode & m : MapsViewer::displayModes())
    m_view->Append(wxString::FromUTF8(m.label));
  m_view->SetSelection(maps->displayMode());
  m_view->SetToolTip(_("How the terrain is drawn: its textures, or one of the geometry and material diagnostics. "
                       "Changing it draws the tile on screen again without reading it again."));
  m_viewNote = UiStyle::secondaryLabel(this, wxEmptyString);
  m_clear = new UiButton(this, wxID_ANY, _("Clear terrain"), UiButton::Kind::Secondary);
  m_clear->SetName(wxT("mapClearTerrain"));
  m_clear->SetToolTip(_("Take the world away from the viewport"));

  wxBoxSizer * fields = new wxBoxSizer(wxHORIZONTAL);
  const auto addField = [&](const wxString & axis, wxTextCtrl * t) {
    fields->Add(new wxStaticText(this, wxID_ANY, axis), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, xs);
    fields->Add(t, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, sp);
  };
  addField(wxT("X"), m_tx);
  addField(wxT("Y"), m_ty);
  addField(wxT("Z"), m_tz);
  wxBoxSizer * buttons = new wxBoxSizer(wxHORIZONTAL);
  buttons->Add(m_enter, 0, wxRIGHT, sp);
  buttons->Add(m_back, 0);
  wxBoxSizer * camera = new wxBoxSizer(wxHORIZONTAL);
  camera->Add(cameraLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, sp);
  camera->Add(m_camera, 1, wxALIGN_CENTER_VERTICAL);
  wxBoxSizer * view = new wxBoxSizer(wxHORIZONTAL);
  view->Add(viewLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, sp);
  view->Add(m_view, 1, wxALIGN_CENTER_VERTICAL);

  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
  top->Add(UiStyle::separator(this), 0, wxEXPAND | wxBOTTOM, sp);
  top->Add(m_title, 0, wxEXPAND | wxLEFT | wxRIGHT, sp);
  top->Add(m_facts, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(targetLabel, 0, wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(fields, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(m_target, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(buttons, 0, wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(m_tile, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(camera, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(m_cameraNote, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(view, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
  top->Add(m_viewNote, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);
  top->Add(m_clear, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, sp);
  SetSizer(top);

  for (wxTextCtrl * t : { m_tx, m_ty, m_tz })
  {
    t->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { m_fieldsEdited = true; });
    t->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { ApplyTargetFields(); });
  }
  m_enter->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    if (m_fieldsEdited && !ApplyTargetFields())
      return;
    m_maps->EnterWorld();
  });
  m_back->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { m_maps->BackToMap(); });
  m_camera->Bind(wxEVT_CHOICE, [this](wxCommandEvent & e) {
    m_maps->SetCameraMode(e.GetSelection() == 1 ? MapsViewer::CameraMode::Overview : MapsViewer::CameraMode::Fly);
  });
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

bool MapTilePanel::ApplyTargetFields()
{
  double x = 0.0, y = 0.0, z = 0.0;
  const bool hasZ = !m_tz->GetValue().Trim(true).Trim(false).IsEmpty();
  // All three emptied: no target (a WMO-only map is entered at its start point again).
  if (!hasZ && m_tx->GetValue().Trim(true).Trim(false).IsEmpty() && m_ty->GetValue().Trim(true).Trim(false).IsEmpty())
  {
    m_fieldsEdited = false;
    m_maps->ClearTarget();
    return true;
  }
  if (!parseNumber(m_tx->GetValue(), x) || !parseNumber(m_ty->GetValue(), y) || (hasZ && !parseNumber(m_tz->GetValue(), z)))
  {
    setLabel(m_target, _("X and Y must be numbers (yards), and Z a number or empty for the ground."));
    Layout();
    return false;
  }
  m_fieldsEdited = false;
  return m_maps->SetTarget(x, y, hasZ, z);
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
  // The target, in the fields unless the user is typing one.
  if (!m_fieldsEdited)
  {
    const wxString x = m_maps->hasTarget() ? wxString::Format(wxT("%.2f"), m_maps->targetX()) : wxString();
    const wxString y = m_maps->hasTarget() ? wxString::Format(wxT("%.2f"), m_maps->targetY()) : wxString();
    const wxString z = m_maps->hasTarget() && m_maps->targetHasZ() ? wxString::Format(wxT("%.2f"), m_maps->targetZ()) : wxString();
    if (m_tx->GetValue() != x) m_tx->ChangeValue(x);
    if (m_ty->GetValue() != y) m_ty->ChangeValue(y);
    if (m_tz->GetValue() != z) m_tz->ChangeValue(z);
  }
  setLabel(m_target, m_maps->hasTarget() ? qs(m_maps->targetLine())
                     : m_maps->worldModelMap() ? _("Click the world model's footprint, or type X and Y, to set where to enter it; "
                                                   "Enter world without a target (X and Y empty) starts at its start point.")
                     : m_maps->hasGrid() ? _("Click the overhead map, or type X and Y, to set where to enter the world.")
                                         : wxString());
  const wxString zHint = m_maps->worldModelMap() ? _("floor") : _("ground");
  if (m_tz->GetHint() != zHint)
    m_tz->SetHint(zHint);
  QString why;
  const bool canEnter = m_maps->canEnter(why);
  m_enter->Enable(canEnter || m_fieldsEdited);
  const wxString enterTip = !canEnter ? qs(why)
                            : m_maps->worldModelMap() && !m_maps->hasTarget()
                              ? _("Load the world model and put the free camera at its start point")
                            : m_maps->worldModelMap() ? _("Load the world model and put the free camera at the target")
                                                      : _("Load the world at the target and put the free camera there");
  if (m_enter->GetToolTipText() != enterTip)
    m_enter->SetToolTip(enterTip);
  m_back->Enable(m_maps->worldShown());
  QString tile;
  switch (m_maps->tilePhase())
  {
    case MapsViewer::TilePhase::None:
      tile = m_maps->hasTarget() && !canEnter ? why : QString();
      break;
    case MapsViewer::TilePhase::Loading:
      // A world load (protocol 16): its world models' progress under it.
      tile = m_maps->tileDetail() + (m_maps->worldDetail().isEmpty() ? QString() : "\n" + m_maps->worldDetail());
      break;
    default:
      tile = m_maps->tileDetail();
      break;
  }
  setLabel(m_tile, qs(tile));
  const int cameraIndex = m_maps->cameraMode() == MapsViewer::CameraMode::Overview ? 1 : 0;
  if (m_camera->GetSelection() != cameraIndex)
    m_camera->SetSelection(cameraIndex);
  m_camera->Enable(m_maps->worldShown());
  const wxString overview = m_maps->worldModelMap() ? _("World model overview") : _("Tile overview");
  if (m_camera->GetString(1) != overview)
    m_camera->SetString(1, overview);
  const wxString cameraTip = m_maps->worldModelMap()
                               ? _("The free camera flies from the target, or the world model's start point; the overview "
                                   "shows the whole world model from outside")
                               : _("The free camera flies from the target; the overview shows the whole tile from above");
  if (m_camera->GetToolTipText() != cameraTip)
    m_camera->SetToolTip(cameraTip);
  // The display modes draw the terrain: a WMO-only map has none.
  m_view->Enable(!m_maps->worldModelMap());
  setLabel(m_cameraNote, qs(m_maps->cameraDetail()));
  setLabel(m_viewNote, qs(m_maps->viewDetail()));
  if (m_view->GetSelection() != m_maps->displayMode())
    m_view->SetSelection(m_maps->displayMode());
  m_clear->Enable(m_maps->shownTileX() >= 0 || m_maps->worldModelShown() || m_maps->tilePhase() == MapsViewer::TilePhase::Loading);
  Layout();
  // A text that grew or shrank by lines (a world load's result is several) changes the height the panel needs: the Browse
  // pane lays it out again, the map list taking the rest -- only then, not on every refresh.
  const int height = GetSizer()->GetMinSize().y;
  if (height != m_laidOutHeight)
  {
    m_laidOutHeight = height;
    InvalidateBestSize();
    if (GetParent())
      GetParent()->Layout();
  }
}
