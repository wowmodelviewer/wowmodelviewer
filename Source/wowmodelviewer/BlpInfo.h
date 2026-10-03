/*
 * BlpInfo.h
 *
 * What a BLP texture's header says, read from its first bytes, and whether the app's texture decoder
 * (Texture::load) reads that kind of BLP correctly. The Texture Viewer uses it to describe a texture
 * and to refuse -- with the reason -- what it cannot show truthfully, instead of displaying or
 * exporting pixels the decoder got wrong.
 *
 * Nothing here decodes pixels: the header is a fixed 148-byte layout and every value shown is read
 * from it. "Supported" is a statement about the existing decoder, made from its code:
 *   - encoding 1 (palettised) with alpha depth 0, 1 or 8. Depth 4 is misread on every second pixel
 *     (Texture.cpp, the 4-bit branch), other depths come out fully transparent.
 *   - encoding 2 (DXT) in the combinations the decoder documents (DXT1 for alpha depth 0 or 1 with
 *     alpha type 0, DXT3 for 8 or 4 with alpha type 0 or 1, DXT5 for 8 with alpha type 7); others
 *     reach a DXT kind through its "guesswork" branch.
 *   - and only when the first mip level is whole (inside the file, after the header, holding the
 *     bytes the decoder consumes) and no level is larger than the first (the decoder reads each into a
 *     buffer that size). A smaller level that is damaged leaves the picture intact: intactLevels
 *     stops before it, and nothing past it is used.
 *   - not encoding 3 (uncompressed BGRA): the decoder has no branch for it and uploads nothing.
 *   - not type 0 (JPEG): that branch uploads no pixels.
 */

#ifndef BLPINFO_H
#define BLPINFO_H

#include <QString>

struct BlpInfo
{
  // Read from the header.
  bool headerRead = false;      // at least the 148 header bytes were there
  QString magic;                // "BLP2" for every texture of a modern client
  unsigned int type = 0;        // 1 = DXT/palettised/raw, 0 = JPEG
  unsigned int encoding = 0;    // 1 palettised, 2 DXT, 3 uncompressed BGRA
  unsigned int alphaDepth = 0;  // bits of alpha the encoding carries: 0, 1, 4 or 8
  unsigned int alphaType = 0;   // the DXT selector when encoding is 2
  unsigned int mipsFlag = 0;    // non-zero when the file carries mipmaps
  unsigned int width = 0;
  unsigned int height = 0;
  unsigned int mipOffsets[16] = {};
  unsigned int mipSizes[16] = {};
  int mipLevels = 0;            // levels with data, counted from the offset/size table
  int intactLevels = 0;         // levels the decoder uploads from whole data: up to the first damaged one
  QString mipDamage;            // what is wrong with the first damaged level, when one is
  qint64 fileSize = -1;         // bytes in the file, when known

  // The decoder's verdict.
  bool supported = false;
  QString format;               // "DXT5", "Palettised", "Uncompressed BGRA", ...
  QString reason;               // why it is not supported (empty when it is)
  QString shortReason;          // the same in a few words, for a list row

  // Parse the first bytes of a file. 'fileSize' is the whole file's size when the caller knows it
  // (-1 otherwise); it lets the mip table be checked against the file.
  static BlpInfo fromHeader(const unsigned char * bytes, size_t count, qint64 fileSize);

  // One line for the info panel: "DXT5, 8-bit alpha" / "Palettised, no alpha" / ...
  QString describeFormat() const;

private:
  static BlpInfo parse(const unsigned char * bytes, size_t count, qint64 fileSize);
};

#endif // BLPINFO_H
