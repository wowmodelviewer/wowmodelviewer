/*
 * UiIcons.cpp
 *
 * The icons (UiIcons.h): Lucide icons (lucide-static 1.51.0), kept as the elements of their SVG, wrapped
 * here with the colour and stroke asked for (the SVG renderer in wxWidgets does not know currentColor),
 * and cached per icon, colour and size. On a 24 grid the stroke is 1.5 (one pixel at 16): the icon
 * family's own 2 would come out soft at that size.
 *
 * Licence of the icon drawings (both notices are required to accompany them):
 *
 * ISC License
 *
 * Copyright (c) 2026 Lucide Icons and Contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee
 * is hereby granted, provided that the above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * The icons chevron-left, chevron-right, download, maximize, square and x are derived from the Feather
 * project:
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-present Cole Bemis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "UiIcons.h"

#include <map>
#include <tuple>

namespace
{
  struct IconSource
  {
    const char * name;
    bool filled;            // transport shapes are solid
    const char * elements;  // the SVG's drawing elements as published, except that numbers written together
                            // (".486.9") are spaced apart: the renderer reads them as one
  };

  // In UiIcon order (UiIcon::None has none).
  const IconSource Sources[] = {
    { "box", false, R"svg(<path d="M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"/><path d="m3.3 7 8.7 5 8.7-5"/><path d="M12 22V12"/>)svg" },
    { "image", false, R"svg(<rect width="18" height="18" x="3" y="3" rx="2" ry="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"/>)svg" },
    { "maximize", false, R"svg(<path d="M8 3H5a2 2 0 0 0-2 2v3"/><path d="M21 8V5a2 2 0 0 0-2-2h-3"/><path d="M3 16v3a2 2 0 0 0 2 2h3"/><path d="M16 21h3a2 2 0 0 0 2-2v-3"/>)svg" },
    { "camera", false, R"svg(<path d="M13.997 4a2 2 0 0 1 1.76 1.05l.486 .9A2 2 0 0 0 18.003 7H20a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2h1.997a2 2 0 0 0 1.759-1.048l.489-.904A2 2 0 0 1 10.004 4z"/><circle cx="12" cy="13" r="3"/>)svg" },
    { "list-tree", false, R"svg(<path d="M8 5h13"/><path d="M13 12h8"/><path d="M13 19h8"/><path d="M3 10a2 2 0 0 0 2 2h3"/><path d="M3 5v12a2 2 0 0 0 2 2h3"/>)svg" },
    { "sliders-horizontal", false, R"svg(<path d="M10 5H3"/><path d="M12 19H3"/><path d="M14 3v4"/><path d="M16 17v4"/><path d="M21 12h-9"/><path d="M21 19h-5"/><path d="M21 5h-7"/><path d="M8 10v4"/><path d="M8 12H3"/>)svg" },
    { "clapperboard", false, R"svg(<path d="m12.296 3.464 3.02 3.956"/><path d="M20.2 6 3 11l-.9-2.4c-.3-1.1 .3-2.2 1.3-2.5l13.5-4c1.1-.3 2.2 .3 2.5 1.3z"/><path d="M3 11h18v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><path d="m6.18 5.276 3.1 3.899"/>)svg" },
    { "download", false, R"svg(<path d="M12 15V3"/><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m7 10 5 5 5-5"/>)svg" },
    { "copy", false, R"svg(<rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1 .9-2 2-2h10c1.1 0 2 .9 2 2"/>)svg" },
    { "play", true, R"svg(<path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"/>)svg" },
    { "pause", true, R"svg(<rect x="14" y="3" width="5" height="18" rx="1"/><rect x="5" y="3" width="5" height="18" rx="1"/>)svg" },
    { "square", true, R"svg(<rect width="18" height="18" x="3" y="3" rx="2"/>)svg" },
    { "chevron-left", false, R"svg(<path d="m15 18-6-6 6-6"/>)svg" },
    { "chevron-right", false, R"svg(<path d="m9 18 6-6-6-6"/>)svg" },
    { "rotate-ccw", false, R"svg(<path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/>)svg" },
    { "x", false, R"svg(<path d="M18 6 6 18"/><path d="m6 6 12 12"/>)svg" },
  };

  wxString svgFor(const IconSource & source, const wxColour & colour, int sizeDip)
  {
    const wxString hex = colour.GetAsString(wxC2S_HTML_SYNTAX);
    // One device pixel of stroke at 16: 1.5 on the 24 grid; a little more at larger sizes. Written as text:
    // a number formatted under a German or French interface would read "1,5", which the SVG reader cuts
    // short at the comma.
    const wxString stroke = sizeDip >= 20 ? wxT("1.75") : wxT("1.5");
    // A solid shape weighs more than an outline of the same size: the transport glyphs are drawn at
    // three quarters, about centre, so they sit beside the outline icons at the same visual weight.
    wxString elements = wxString::FromUTF8(source.elements);
    if (source.filled)
      elements = wxT("<g transform=\"translate(12 12) scale(0.75) translate(-12 -12)\">") + elements + wxT("</g>");
    return wxString::Format(wxT("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" ")
                            wxT("fill=\"%s\" stroke=\"%s\" stroke-width=\"%s\" stroke-linecap=\"round\" ")
                            wxT("stroke-linejoin=\"round\">%s</svg>"),
                            source.filled ? hex : wxString(wxT("none")), hex, stroke, elements);
  }
}

namespace UiIcons
{
  wxBitmapBundle bundle(UiIcon icon, const wxColour & colour, int sizeDip)
  {
    const int index = (int)icon - 1;
    if (icon == UiIcon::None || index < 0 || index >= (int)WXSIZEOF(Sources) || !colour.IsOk())
      return wxBitmapBundle();
    static std::map<std::tuple<int, wxUint32, int>, wxBitmapBundle> cache;
    const auto key = std::make_tuple(index, colour.GetRGBA(), sizeDip);
    auto found = cache.find(key);
    if (found != cache.end())
      return found->second;
    const wxScopedCharBuffer svg = svgFor(Sources[index], colour, sizeDip).utf8_str();
    wxBitmapBundle b = wxBitmapBundle::FromSVG(svg.data(), wxSize(sizeDip, sizeDip));
    cache[key] = b;
    return b;
  }

  wxBitmap bitmap(UiIcon icon, const wxColour & colour, const wxWindow * window, int sizeDip)
  {
    const wxBitmapBundle b = bundle(icon, colour, sizeDip);
    if (!b.IsOk())
      return wxNullBitmap;
    const int px = window ? window->FromDIP(sizeDip) : sizeDip;
    return b.GetBitmap(wxSize(px, px));
  }
}
