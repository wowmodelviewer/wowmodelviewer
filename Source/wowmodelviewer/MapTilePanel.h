/*
 * MapTilePanel.h
 *
 * THE MAPS VIEWER'S TILE GRID (docs/world-viewer/05-maps-viewer-ui.md): under the map list in Browse while the Maps viewer
 * is on. It shows the map picked (its name, MapID, directory and WDT), the 64 x 64 ADT tile grid as the WDT and the
 * client's storage give it (MapsViewer: x the column, growing east; y the row, growing south -- the root ADT of the cell in
 * column x, row y is <map>_<x>_<y>.adt), the tile under the mouse and the one picked, the load's state, the display mode
 * and Clear terrain. Owns no state of its own but the hover and the zoom: MapsViewer does.
 *
 * The grid is one owner-drawn window (no control per tile): by default fitted to the box of the tiles the WDT flags (with
 * the coordinates on its edges), or the whole grid. The wheel zooms in about the tile under the mouse (cells up to
 * MaxCell pixels) and back out to the fit; a drag moves a zoomed grid. A click on an available tile loads it; on any
 * other, says why it cannot be. Arrow keys move the keyboard's tile; Enter or Space loads it.
 */

#ifndef MAPTILEPANEL_H
#define MAPTILEPANEL_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

class MapsViewer;
class UiButton;
class wxCheckBox;
class wxChoice;

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
  // The tile at a point of this window (false: none); and the rectangle of a tile (the tests click its centre).
  bool TileAt(const wxPoint & p, int & x, int & y) const;
  wxRect TileRect(int x, int y) const;
  int hoverX() const { return m_hoverX; }
  int hoverY() const { return m_hoverY; }
  // The extent the grid shows now (tile coordinates, inclusive) and its cell size in pixels.
  void Extent(int & x0, int & y0, int & x1, int & y1) const;
  int cellSize() const { return m_cell; }

private:
  void layoutCells() const;
  void OnPaint(wxPaintEvent & event);
  void OnMouse(wxMouseEvent & event);
  void OnWheel(wxMouseEvent & event);
  int fitCell() const;
  void OnKey(wxKeyEvent & event);
  void OnSize(wxSizeEvent & event);

  MapsViewer * m_maps;
  bool m_whole = false;
  int m_zoom = 0;                    // cell size asked for by the wheel, 0: fitted
  double m_cx = 32.0, m_cy = 32.0;   // the tile at the centre of a zoomed view
  wxPoint m_down;                    // where the left button went down
  bool m_pressed = false, m_panned = false;
  double m_downCx = 0.0, m_downCy = 0.0;
  int m_hoverX = -1, m_hoverY = -1;
  int m_keyX = -1, m_keyY = -1;
  // Laid out on demand (layoutCells): the extent, the cell size and where cell (x0, y0) is drawn.
  mutable int m_x0 = 0, m_y0 = 0, m_x1 = 63, m_y1 = 63, m_cell = 4, m_left = 0, m_top = 0;
};

class MapTilePanel : public wxPanel
{
public:
  MapTilePanel(wxWindow * parent, MapsViewer * maps);

  // MapsViewer changed (its onChanged): the labels, the grid and the controls follow.
  void Refresh2();
  MapTileGrid * grid() const { return m_grid; }
  wxChoice * viewChoice() const { return m_view; }
  UiButton * clearButton() const { return m_clear; }
  wxCheckBox * wholeGridBox() const { return m_wholeBox; }
  // The hovered tile's words (the grid tells the panel).
  void HoverChanged();

private:
  void setLabel(wxStaticText * label, const wxString & text);

  MapsViewer * m_maps;
  wxStaticText * m_title = nullptr;
  wxStaticText * m_facts = nullptr;
  wxStaticText * m_status = nullptr;
  MapTileGrid * m_grid = nullptr;
  wxCheckBox * m_wholeBox = nullptr;
  wxStaticText * m_tile = nullptr;
  wxStaticText * m_hover = nullptr;
  wxChoice * m_view = nullptr;
  wxStaticText * m_viewNote = nullptr;
  UiButton * m_clear = nullptr;
  bool m_refreshQueued = false;   // a size event's refresh waits (one at a time)
};

#endif // MAPTILEPANEL_H
