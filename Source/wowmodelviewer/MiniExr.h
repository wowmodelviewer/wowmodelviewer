/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
| Minimal, self-contained OpenEXR writer (no external dependency).       |
\*----------------------------------------------------------------------*/

/*
 * MiniExr.h
 *
 *  A tiny writer for uncompressed, scanline, 32-bit-float RGBA OpenEXR files.
 *  EXR is the only image-sequence format WMV exports that Qt/CxImage cannot write,
 *  and pulling in full OpenEXR/tinyexr is heavy, so this writes the documented
 *  EXR 2.0 uncompressed layout directly. Output is linear float RGBA -- the format
 *  After Effects / Resolve / Nuke expect for a linear compositing workflow.
 */

#ifndef _MINIEXR_H_
#define _MINIEXR_H_

#include <string>

namespace MiniExr
{
  // Writes a linear 32-bit-float RGBA .exr (uncompressed, increasing-Y scanlines).
  //   rgba   : width*height*4 floats, channel order R,G,B,A.
  //   bottomUp: true if row 0 of `rgba` is the BOTTOM of the image (as glReadPixels
  //             returns); the writer flips it to EXR's top-down data window.
  // Returns false on any I/O error.
  bool writeRGBAF(const std::wstring & path, int width, int height,
                  const float * rgba, bool bottomUp);
}

#endif /* _MINIEXR_H_ */
