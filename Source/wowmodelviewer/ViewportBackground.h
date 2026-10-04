/*
 * ViewportBackground.h
 *
 * THE MODELS VIEWPORT'S BACKGROUND: the colour the Unity player clears the Models viewport to, chosen in
 * View > Swap Background Color (BackgroundColorDialog). This is the part with no window in it: the default, the
 * built-in presets, the strict #RRGGBB reading and writing, and where the choice and the user's own presets are kept.
 *
 * A colour here is always what the viewport is to DISPLAY, as opaque sRGB bytes: #808080 shows as 128 on screen.
 * The player works out the camera clear that lands on those bytes (WmvMain.ApplyViewportBackground, protocol 7).
 * Nothing here applies to the Texture Viewer, whose backgrounds are its own (TextureViewer/*).
 *
 * Kept in Config.ini, written when the user changes them (never at exit, so a headless or test run that only shows
 * a colour leaves the file as it was):
 *   ModelViewport/BackgroundColor    "#RRGGBB"; absent or unreadable: the default
 *   ModelViewport/BackgroundPresets  the user's own presets, "#RRGGBB" each, in the order they were saved
 * The archived OpenGL viewport's Session/bgCol and Session/bgCustCol* are not read (modelviewer.cpp LoadSession).
 */

#ifndef VIEWPORTBACKGROUND_H
#define VIEWPORTBACKGROUND_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <vector>

namespace ViewportBackground
{
  // The background the viewport has always had, as displayed: #19191E.
  wxColour defaultColour();

  struct Preset
  {
    wxString name;      // shown in the tooltip; translated
    wxColour colour;
  };
  // Default #19191E, Dark #000000, Slate #202428, Neutral Grey #808080, Light #BBBBBB. Not removable.
  const std::vector<Preset> & builtIns();
  // The built-in preset with this colour, or null.
  const Preset * builtInFor(const wxColour & colour);

  // The most presets of the user's own that are kept: two rows of the window's swatches.
  const size_t MaxCustomPresets = 18;

  // Strict: six hex digits, either case, with or without a leading '#', spaces around ignored. False for anything
  // else (#RGB, colour names, rgb(...)), and colour is then left as it was.
  bool parseHex(const wxString & text, wxColour & colour);
  // "#RRGGBB", upper case.
  wxString formatHex(const wxColour & colour);
  // The same colour, ignoring alpha (every colour here is opaque).
  bool sameColour(const wxColour & a, const wxColour & b);

  // From Config.ini (the path is util.h's cfgPath).
  wxColour loadColour();
  void saveColour(const wxColour & colour);
  // In the order saved; unreadable entries, duplicates and built-in colours dropped.
  std::vector<wxColour> loadCustomPresets();
  void saveCustomPresets(const std::vector<wxColour> & presets);
}

#endif // VIEWPORTBACKGROUND_H
