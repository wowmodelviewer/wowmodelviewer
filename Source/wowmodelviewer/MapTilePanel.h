/*
 * MapTilePanel.h
 *
 * THE MAPS VIEWER'S CONTROLS AND ITS TILE GRID (docs/world-viewer/05-maps-viewer-ui.md).
 *
 * MapTileGrid: the overhead map -- the 64 x 64 ADT tile grid as the WDT and the client's storage give it (MapsViewer: x
 * the column, growing east; y the row, growing south -- the root ADT of the cell in column x, row y is
 * <map>_<x>_<y>.adt), with each tile's minimap picture under it where the client has one, faint tile borders, the target
 * (a ring round its tile, a marker at its point), the tile on screen and, after a flight, where the camera was put. One
 * owner-drawn window (no control per tile): by default fitted to the box of the tiles the WDT flags (with the
 * coordinates on its edges), or the whole grid. The wheel zooms in about the tile under the mouse (cells up to MaxCell
 * pixels) and back out to the fit; a drag moves a zoomed grid. A click sets the target at the point clicked; a
 * double-click also enters the world there. Arrow keys move the target a tile; Enter enters the world. The pictures are
 * kept as one mosaic bitmap over the map's box (a thumbnail per tile, drawn into it once as it comes), so a paint is one
 * stretched copy of it however many tiles show. Without a grid it shows why (MapsViewer::viewportNotice).
 *
 * A WMO-ONLY MAP (protocol 16): no tile grid -- the world model's FOOTPRINT instead, fitted to the window, north up: its
 * placed box, and every group's box seen from above (interior groups apart from exterior ones, the higher drawn over the
 * lower), the target and the camera after a flight, a north mark and a scale bar. A click inside the world model's box
 * sets the target there; a double-click also enters the world; the arrow keys move the target, Enter enters.
 *
 * MapTilePanel: under the map list in Browse while the Maps viewer is on -- the map picked (its name, MapID, directory and
 * WDT), its state, the target (typed X, Y and Z, or the point clicked), Enter world and Back to map, the camera (the free
 * camera or the tile's overview), the display mode and Clear terrain. Owns no state of its own: MapsViewer does.
 */

#ifndef MAPTILEPANEL_H
#define MAPTILEPANEL_H

#include <functional>
#include <vector>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/timer.h>

class MapsViewer;
class UiButton;
class wxCheckBox;
class wxChoice;
class wxTextCtrl;

// The grid itself.
class MapTileGrid : public wxWindow
{
public:
  MapTileGrid(wxWindow * parent, MapsViewer * maps);

  void SetWholeGrid(bool whole);
  bool wholeGrid() const { return m_whole; }
  // Zoomed in (the wheel), with the tile at the centre of the view; 0: fitted.
  int zoomCell() const { return m_zoom; }
  // Zoom so cells are this big (0: back to the fit), keeping tile (x, y) at the centre (the tests).
  void ZoomTo(int cell, double x, double y);
  // The tile at a point of this window (false: none), with the point's fractions inside it (east, south); and the
  // rectangle of a tile (the tests click its centre).
  bool TileAt(const wxPoint & p, int & x, int & y) const;
  bool PointAt(const wxPoint & p, int & x, int & y, double & fx, double & fy) const;
  wxRect TileRect(int x, int y) const;
  int hoverX() const { return m_hoverX; }
  int hoverY() const { return m_hoverY; }
  // The extent the grid shows now (tile coordinates, inclusive) and its cell size in pixels.
  void Extent(int & x0, int & y0, int & x1, int & y1) const;
  int cellSize() const { return m_cell; }
  // The hovered tile changed (the panel's hover line follows).
  std::function<void()> onHover;
  // A WMO-only map's footprint: the world point (yards) at a point of this window (false: outside the world model's box),
  // and the window point of a world point (the tests click it); the world point under the mouse (false: none).
  bool FootprintPointAt(const wxPoint & p, double & x, double & y) const;
  wxPoint FootprintPoint(double x, double y) const;
  bool footprintHover(double & x, double & y) const
  {
    x = m_hoverWX;
    y = m_hoverWY;
    return m_hoverWorld;
  }
  // Pictures came (MapsViewer::onImagesChanged): drawn into the mosaic at the next paint, at most one every RepaintMs.
  void ImagesChanged();
  static const int RepaintMs = 100;
  // At most this long drawing pictures into the mosaic in one paint (one 60 Hz frame); the rest at the next repaint.
  static const int MosaicPaintBudgetMs = 16;

private:
  void layoutCells() const;
  void OnPaint(wxPaintEvent & event);
  void OnMouse(wxMouseEvent & event);
  void OnWheel(wxMouseEvent & event);
  int fitCell() const;
  void OnKey(wxKeyEvent & event);
  void OnSize(wxSizeEvent & event);
  void updateMosaic();
  void hoverTo(int x, int y);
  // The footprint's fit: yards to pixels, and the window point of the box's north-west corner (x max, y max).
  bool footprintFit(double & scale, double & left, double & top) const;
  void paintFootprint(wxDC & dc);
  void footprintMouse(wxMouseEvent & event);
  void footprintKey(wxKeyEvent & event);

  MapsViewer * m_maps;
  bool m_whole = false;
  int m_zoom = 0;                    // cell size asked for by the wheel, 0: fitted
  double m_cx = 32.0, m_cy = 32.0;   // the tile at the centre of a zoomed view
  wxPoint m_down;                    // where the left button went down
  bool m_pressed = false, m_panned = false;
  double m_downCx = 0.0, m_downCy = 0.0;
  int m_hoverX = -1, m_hoverY = -1;
  // Laid out on demand (layoutCells): the extent, the cell size and where cell (x0, y0) is drawn.
  mutable int m_x0 = 0, m_y0 = 0, m_x1 = 63, m_y1 = 63, m_cell = 4, m_left = 0, m_top = 0;
  // The pictures: one bitmap over the map's box (m_mx0.. in tiles), ThumbSide pixels a tile; which tiles are in it.
  wxBitmap m_mosaic;
  int m_mosaicSerial = -1;           // MapsViewer::imagesSerial() it was last brought up to
  int m_mosaicMap = -2;              // the map (and its inspection) it is of
  int m_mosaicInspection = -1;
  int m_mx0 = 0, m_my0 = 0, m_mx1 = -1, m_my1 = -1;
  std::vector<bool> m_inMosaic;
  wxTimer m_repaintTimer;
  bool m_hoverWorld = false;         // the footprint: the world point under the mouse
  double m_hoverWX = 0.0, m_hoverWY = 0.0;
};

class MapTilePanel : public wxPanel
{
public:
  MapTilePanel(wxWindow * parent, MapsViewer * maps);

  // MapsViewer changed (its onChanged): the labels and the controls follow.
  void Refresh2();
  // STREAMING (protocol 18): the session's line alone (MapsViewer::onStreamChanged), laid out only when its height changed.
  void StreamChanged();
  wxChoice * viewChoice() const { return m_view; }
  wxChoice * cameraChoice() const { return m_camera; }
  UiButton * clearButton() const { return m_clear; }
  UiButton * enterButton() const { return m_enter; }
  UiButton * backButton() const { return m_back; }
  wxTextCtrl * targetField(int axis) const { return axis == 0 ? m_tx : axis == 1 ? m_ty : m_tz; }
  // The fields' X, Y and Z made the target (Enter in a field, or Enter world); false, and said, when they are not numbers.
  bool ApplyTargetFields();

private:
  void setLabel(wxStaticText * label, const wxString & text);
  // STREAMING: the session's line set (true when it appeared or went: the panel needs laying out).
  void setStreamLine();

  MapsViewer * m_maps;
  wxStaticText * m_title = nullptr;
  wxStaticText * m_facts = nullptr;
  wxStaticText * m_status = nullptr;
  wxTextCtrl * m_tx = nullptr;
  wxTextCtrl * m_ty = nullptr;
  wxTextCtrl * m_tz = nullptr;
  wxStaticText * m_target = nullptr;
  UiButton * m_enter = nullptr;
  UiButton * m_back = nullptr;
  wxStaticText * m_tile = nullptr;
  wxStaticText * m_stream = nullptr;   // STREAMING: the session's line
  wxChoice * m_camera = nullptr;
  wxStaticText * m_cameraNote = nullptr;
  wxChoice * m_view = nullptr;
  wxStaticText * m_viewNote = nullptr;
  UiButton * m_clear = nullptr;
  bool m_refreshQueued = false;   // a size event's refresh waits (one at a time)
  int m_laidOutHeight = -1;       // the height the Browse pane last laid the panel out for
  bool m_fieldsEdited = false;    // the user typed in a target field since the target was last shown in them
};

#endif // MAPTILEPANEL_H
