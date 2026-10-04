/*
 * UiIcons.h
 *
 * The viewer's icons: a few, where an icon is recognised faster than a word (the command bar, the
 * exports, the transport), all from one icon family drawn on a 24 grid with round strokes, so they
 * read as one set. They are vector (SVG text, a few hundred bytes each) and drawn at the size and in
 * the colour asked for, crisp at any scale. See UiIcons.cpp for their source and licence.
 */

#ifndef UIICONS_H
#define UIICONS_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/bmpbndl.h>

enum class UiIcon
{
  None,
  Models,        // the Models viewer (a box)
  Textures,      // the Textures viewer (a picture)
  Fullscreen,
  Screenshot,
  Browse,        // the Browse panel (a tree)
  ModelPanel,    // the Model panel (sliders)
  Animation,     // the Animation panel (a clapperboard)
  Export,
  Copy,
  Play,
  Pause,
  Stop,
  StepBack,
  StepForward,
  Reset,
  Close
};

namespace UiIcons
{
  // The icon in this colour, sizeDip square (DIP), as a bundle that scales with the window.
  wxBitmapBundle bundle(UiIcon icon, const wxColour & colour, int sizeDip = 16);
  // The same, as a bitmap for this window's scale.
  wxBitmap bitmap(UiIcon icon, const wxColour & colour, const wxWindow * window, int sizeDip = 16);
}

#endif // UIICONS_H
