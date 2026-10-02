/*
 * BlpInfo.cpp -- see BlpInfo.h.
 */

#include "BlpInfo.h"

#include <cstring>

namespace
{
  // The BLP2 header: magic[4], type, encoding, alphaDepth, alphaType, mipsFlag, width, height,
  // mipOffsets[16], mipSizes[16]. The palette (1024 bytes) follows, then the image data.
  const size_t HeaderBytes = 148;

  unsigned int u32(const unsigned char * p)
  {
    unsigned int v = 0;
    memcpy(&v, p, 4);
    return v;
  }
}

namespace
{
  // A refusal in a few words, for a list row.
  QString shortReasonOf(const BlpInfo & b)
  {
    if (b.supported)
      return QString();
    if (!b.headerRead)
      return "too short to be a texture";
    if (b.magic != "BLP2")
      return "not a texture";
    if (b.type == 0)
      return "JPEG BLP";
    if (b.type != 1)
      return "unknown BLP type";
    if (b.encoding == 3)
      return "uncompressed BGRA";
    if (b.encoding == 1 && b.alphaDepth != 0 && b.alphaDepth != 1 && b.alphaDepth != 8)
      return b.alphaDepth == 4 ? QString("4-bit palettised alpha") : QString("unsupported alpha depth");
    if (b.encoding == 2 && b.format == "DXT")
      return "unverified DXT kind";
    if (b.encoding != 1 && b.encoding != 2)
      return "unknown BLP encoding";
    return "damaged mip table";
  }
}

BlpInfo BlpInfo::fromHeader(const unsigned char * bytes, size_t count, qint64 fileSize)
{
  BlpInfo b = parse(bytes, count, fileSize);
  b.shortReason = shortReasonOf(b);
  return b;
}

BlpInfo BlpInfo::parse(const unsigned char * bytes, size_t count, qint64 fileSize)
{
  BlpInfo b;
  b.fileSize = fileSize;
  if (!bytes || count < HeaderBytes)
  {
    b.format = "Unknown";
    b.reason = QString("The file is %1 bytes, too short for a BLP texture header.").arg((qulonglong)count);
    return b;
  }

  b.headerRead = true;
  b.magic = QString::fromLatin1(reinterpret_cast<const char *>(bytes), 4);
  b.type = u32(bytes + 4);
  b.encoding = bytes[8];
  b.alphaDepth = bytes[9];
  b.alphaType = bytes[10];
  b.mipsFlag = bytes[11];
  b.width = u32(bytes + 12);
  b.height = u32(bytes + 16);
  for (int i = 0; i < 16; i++)
  {
    b.mipOffsets[i] = u32(bytes + 20 + 4 * i);
    b.mipSizes[i] = u32(bytes + 84 + 4 * i);
  }
  while (b.mipLevels < 16 && b.mipOffsets[b.mipLevels] && b.mipSizes[b.mipLevels])
    b.mipLevels++;

  if (b.magic != "BLP2")
  {
    QString shown;
    for (int i = 0; i < 4; i++)
      shown += (bytes[i] >= 32 && bytes[i] < 127) ? QChar(bytes[i]) : QChar('.');
    b.format = "Unknown";
    b.reason = QString("This is not a BLP2 texture: the file starts with \"%1\".").arg(shown);
    return b;
  }
  if (b.type == 0)
  {
    b.format = "JPEG";
    b.reason = "JPEG-compressed BLP: the app's texture decoder does not read the pixels of this kind.";
    return b;
  }
  if (b.type != 1)
  {
    b.format = QString("Type %1").arg(b.type);
    b.reason = QString("Unknown BLP type %1.").arg(b.type);
    return b;
  }
  if (b.width == 0 || b.height == 0 || b.width > 16384 || b.height > 16384)
  {
    b.format = "Unknown";
    b.reason = QString("The header gives an impossible size, %1 x %2.").arg(b.width).arg(b.height);
    return b;
  }
  if (b.mipLevels == 0)
  {
    b.format = "Unknown";
    b.reason = "The header lists no image data.";
    return b;
  }

  // Which branch of Texture::load would run, and with what block size (Texture.cpp, encoding 2 and 1).
  // The documented combinations only (Texture.cpp's own comments: DXT1 for alpha depth 0 or 1, DXT3 for
  // depth 8 or 4, DXT5 for depth 8 with alpha type 7). Any other combination reaches a DXT kind through
  // the branch the code itself calls guesswork, so it is not shown. DXT1 also needs alpha type 0: the
  // client's files with depth 0 and type 7 or 11 hold 16-byte blocks (twice the DXT1 data), which that
  // branch would read as DXT1.
  int blockBytes = 0;
  switch (b.encoding)
  {
    case 1:
      b.format = "Palettised";
      if (b.alphaDepth == 4)
      {
        b.reason = "Palettised with 4-bit alpha: the app's texture decoder reads the alpha of every second "
                   "pixel wrongly, so the texture is not shown.";
        return b;
      }
      if (b.alphaDepth != 0 && b.alphaDepth != 1 && b.alphaDepth != 8)
      {
        b.reason = QString("Palettised with %1-bit alpha: the app's texture decoder does not read this alpha depth.")
                     .arg(b.alphaDepth);
        return b;
      }
      break;
    case 2:
      if ((b.alphaDepth == 0 || b.alphaDepth == 1) && b.alphaType == 0)
        b.format = "DXT1", blockBytes = 8;
      else if (b.alphaDepth == 8 && b.alphaType == 7)
        b.format = "DXT5", blockBytes = 16;
      else if ((b.alphaDepth == 8 || b.alphaDepth == 4) && (b.alphaType == 0 || b.alphaType == 1))
        b.format = "DXT3", blockBytes = 16;
      else
      {
        b.format = "DXT";
        b.reason = QString("DXT with alpha depth %1 and alpha type %2: the app's texture decoder only guesses "
                           "which DXT kind this is, so its picture cannot be trusted.")
                     .arg(b.alphaDepth).arg(b.alphaType);
        return b;
      }
      break;
    case 3:
      b.format = "Uncompressed BGRA";
      b.reason = "Uncompressed BGRA (BLP encoding 3): the app's texture decoder has no reader for this encoding.";
      return b;
    default:
      b.format = QString("Encoding %1").arg(b.encoding);
      b.reason = QString("Unknown BLP encoding %1.").arg(b.encoding);
      return b;
  }

  // Every level the decoder will upload, checked against what it does with it: it reads each level
  // into a buffer the size of the first, reads the palette right after the header, and consumes the
  // bytes its own sizes say (DXT blocks, or one index per pixel plus the alpha plane). It checks none
  // of this itself. Its loop: up to 16 levels when the mips flag (a signed char) is positive, else 1;
  // it stops at the first level without an offset or a size.
  const qint64 dataStart = (qint64)HeaderBytes + (b.encoding == 1 ? 1024 : 0);
  if (fileSize >= 0 && fileSize < dataStart)
  {
    b.reason = QString("The file is %1 bytes, too short for its own header and palette.").arg(fileSize);
    return b;
  }
  const int decoderLevels = static_cast<signed char>(b.mipsFlag) > 0 ? 16 : 1;
  qint64 w = b.width, h = b.height;
  for (int i = 0; i < decoderLevels; i++)
  {
    if (!b.mipOffsets[i] || !b.mipSizes[i])
      break;
    const qint64 offset = b.mipOffsets[i], size = b.mipSizes[i];
    // Every level, damaged ones and those after them included: the decoder reads each into a buffer
    // the size of the first, so a larger one overruns it.
    if (size > b.mipSizes[0])
    {
      b.reason = QString("Mip level %1 is larger than the first level (%2 > %3 bytes), so the decoder would "
                         "write past its buffer.").arg(i).arg(size).arg(b.mipSizes[0]);
      b.mipDamage.clear();
      b.intactLevels = 0;
      return b;
    }
    // Past a damaged level, that is all that matters: none of these levels is used.
    if (!b.mipDamage.isEmpty())
      continue;
    qint64 needed = 0;
    if (b.encoding == 2)
      needed = ((w + 3) / 4) * ((h + 3) / 4) * blockBytes;
    else
      needed = w * h + (b.alphaDepth == 8 ? w * h : (b.alphaDepth == 1 ? (w * h + 7) / 8 : 0));

    QString problem;
    if (offset < dataStart)
      problem = QString("starts inside the header (offset %1)").arg(offset);
    else if (fileSize >= 0 && offset + size > fileSize)
      problem = QString("runs past the end of the file (%1 + %2 > %3 bytes)").arg(offset).arg(size).arg(fileSize);
    else if (size < needed)
      problem = QString("is %1 bytes; a %2 x %3 %4 level needs %5").arg(size).arg(w).arg(h).arg(b.format).arg(needed);
    if (!problem.isEmpty())
    {
      // The first level is the picture: damaged, nothing the decoder makes of it can be shown.
      if (i == 0)
      {
        b.reason = QString("Mip level %1 %2, so the decoder would read past its data.").arg(i).arg(problem);
        return b;
      }
      // A smaller level that is short, outside the file or inside the header still fits that buffer
      // (the file reads stop at its end), so the decoder stays inside its memory; only that level's
      // pixels, and those after it, are not the file's. The viewer shows and exports level 0, and
      // takes thumbnails only from the levels before this one.
      b.mipDamage = QString("Mip level %1 %2.").arg(i).arg(problem);
      continue;
    }
    b.intactLevels = i + 1;
    w = qMax<qint64>(1, w / 2);
    h = qMax<qint64>(1, h / 2);
  }

  b.supported = true;
  return b;
}

QString BlpInfo::describeFormat() const
{
  if (!headerRead || magic != "BLP2" || type != 1)
    return format;
  if (encoding == 2 && format == "DXT")
    return QString("DXT (alpha depth %1, alpha type %2)").arg(alphaDepth).arg(alphaType);
  QString alpha = alphaDepth == 0 ? QString("no alpha") : QString("%1-bit alpha").arg(alphaDepth);
  // DXT1 with no alpha depth is uploaded as opaque RGB: whatever the blocks hold, alpha is 1.
  return format + ", " + alpha;
}
