/*
 * MapOverview.h
 *
 * THE OVERHEAD MAP (docs/world-viewer/05-maps-viewer-ui.md section 21): what the viewport's place shows in the Maps
 * viewer until a world is entered, and after Back to map -- the viewport host's content window, as the texture viewer's
 * is (UnityRendererHost::setContent), so the Unity player is only taken off screen meanwhile and whatever world it holds
 * stays loaded behind it.
 *
 * Top: the map's name and how to use the map; the whole-grid box; Enter world. Middle: the tile grid with the minimap
 * pictures (MapTileGrid, MapTilePanel.h). Bottom: the tile under the mouse -- its coordinates, its state, its picture's --
 * and the target. Owns no state of its own: MapsViewer does.
 */

#ifndef MAPOVERVIEW_H
#define MAPOVERVIEW_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

class MapsViewer;
class MapTileGrid;
class UiButton;
class wxCheckBox;

class MapOverview : public wxPanel
{
public:
  MapOverview(wxWindow * parent, MapsViewer * maps);

  // MapsViewer changed (its onChanged): the labels, the grid and the button follow.
  void Refresh2();
  // A minimap picture came (MapsViewer::onImagesChanged).
  void ImagesChanged();
  MapTileGrid * grid() const { return m_grid; }
  wxCheckBox * wholeGridBox() const { return m_wholeBox; }
  UiButton * enterButton() const { return m_enter; }

private:
  void HoverChanged();

  MapsViewer * m_maps;
  wxStaticText * m_title = nullptr;
  wxStaticText * m_hint = nullptr;
  wxCheckBox * m_wholeBox = nullptr;
  UiButton * m_enter = nullptr;
  MapTileGrid * m_grid = nullptr;
  wxStaticText * m_hover = nullptr;
  wxStaticText * m_target = nullptr;
};

#endif // MAPOVERVIEW_H
