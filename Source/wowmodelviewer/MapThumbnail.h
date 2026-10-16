/*
 * MapThumbnail.h
 *
 * THE OVERHEAD MAP'S PICTURES (docs/world-viewer/05-maps-viewer-ui.md section 21): a tile's minimap image (MAID word 7,
 * a BLP2 file: 512 x 512 in Retail and Classic Beta, 256 x 256 in Classic Era and MoP Classic, 00-audit.md 4.6) reduced
 * to a small square thumbnail, on the map asset worker's thread. The app's own texture decoder (TextureDecode) needs the
 * hidden canvas's GL context, so it runs on the GUI thread only; a few hundred minimaps there would stall it. This one
 * is plain code on plain bytes.
 *
 * What it decodes. The header is checked by BlpInfo (the same check the texture viewer makes: magic, sizes, the mip
 * table against the file), then level 0 -- or the smallest level that is still at least the thumbnail's size -- in the
 * BLP2 encodings:
 *   - 2 (block compression): DXT1 (alpha depth 0 or 1), DXT3 (alpha depth 4 or 8, alpha type 0 or 1), DXT5 (alpha type
 *     7), as BlpInfo names them;
 *   - 3: uncompressed BGRA;
 *   - 1: palettised, with alpha depth 0, 1 or 8.
 * Anything else is refused with the reason, never guessed. The reduction is a box filter over whole source pixels
 * (side divides the level's side; a level smaller than side is not used), so every thumbnail pixel is the plain mean of
 * the pixels it covers. Alpha is kept as decoded; the overhead map draws the picture opaque (minimaps carry no alpha:
 * alpha type 0).
 *
 * Any thread: no state, no Qt object but the byte arrays it is given.
 */

#ifndef MAPTHUMBNAIL_H
#define MAPTHUMBNAIL_H

#include <QByteArray>
#include <QString>

namespace MapThumbnail
{
  // A side x side RGBA thumbnail (row 0 the image's top, 4 bytes a pixel: R, G, B, A) of the BLP in `bytes`. False, with
  // the reason in `problem`, when the file is not a BLP2 image this decodes, or side is not a power of two from 8 to 256.
  bool fromBlp(const QByteArray & bytes, int side, QByteArray & rgba, QString & problem);

  // A whole level of the BLP in `bytes`, decoded to RGBA (for the tests that compare it with the app's GL decoder).
  bool decodeLevel(const QByteArray & bytes, int level, QByteArray & rgba, int & width, int & height, QString & problem);
}

#endif // MAPTHUMBNAIL_H
