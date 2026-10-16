/*
 * ViewerMode.h
 *
 * THE VIEWER MODE: what the viewer shows, chosen with the command bar's Models | Textures | Buildings | Maps selector.
 *   Models     the Unity viewport with a model (*.m2) and the model panels
 *   Textures   the texture viewer in the viewport's place (TextureView), the model panels put away
 *   Buildings  the Unity viewport with a world model (*.wmo root), the panels that act on a model put away
 *   Maps       the Unity viewport with one terrain tile of a map (MapsViewer: Browse lists the client's maps, the tile
 *              grid under it the map's ADT tiles), the model panels put away (docs/world-viewer/05-maps-viewer-ui.md)
 * ModelViewer::SetViewerMode is the one way to change it; Browse lists what the mode shows
 * (FileControl::FollowViewerMode). On its own here so Browse can take it without the frame's header.
 */

#ifndef VIEWERMODE_H
#define VIEWERMODE_H

enum class ViewerMode
{
  Models,
  Textures,
  Buildings,
  Maps
};

#endif // VIEWERMODE_H
