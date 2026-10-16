/*
 * MapThumbnail.cpp -- minimap thumbnails decoded off the GUI thread (MapThumbnail.h).
 */

#include "MapThumbnail.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "BlpInfo.h"

namespace
{
  const int HeaderSize = 148;
  const int PaletteSize = 256 * 4;

  inline void rgb565(unsigned v, unsigned char & r, unsigned char & g, unsigned char & b)
  {
    const unsigned r5 = (v >> 11) & 31, g6 = (v >> 5) & 63, b5 = v & 31;
    r = (unsigned char)((r5 << 3) | (r5 >> 2));
    g = (unsigned char)((g6 << 2) | (g6 >> 4));
    b = (unsigned char)((b5 << 3) | (b5 >> 2));
  }

  inline unsigned u16(const unsigned char * p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
  inline unsigned u32(const unsigned char * p) { return u16(p) | (u16(p + 2) << 16); }

  enum class Kind { Dxt1, Dxt3, Dxt5, Bgra, Palette, None };

  // The encoding as BlpInfo names it (its format string is the decoder's, so the two never disagree).
  Kind kindOf(const BlpInfo & info, QString & problem)
  {
    if (info.magic != "BLP2" || info.type != 1)
    {
      problem = QString("not a BLP2 image of type 1 (magic \"%1\", type %2)").arg(info.magic).arg(info.type);
      return Kind::None;
    }
    if (info.encoding == 2)
    {
      if (info.alphaType == 7)
        return Kind::Dxt5;
      if (info.alphaDepth == 4 || info.alphaDepth == 8)
        return Kind::Dxt3;
      if (info.alphaDepth <= 1)
        return Kind::Dxt1;
      problem = QString("block compression with alpha depth %1 and alpha type %2").arg(info.alphaDepth).arg(info.alphaType);
      return Kind::None;
    }
    if (info.encoding == 3)
      return Kind::Bgra;
    if (info.encoding == 1 && (info.alphaDepth == 0 || info.alphaDepth == 1 || info.alphaDepth == 8))
      return Kind::Palette;
    problem = QString("encoding %1 with alpha depth %2").arg(info.encoding).arg(info.alphaDepth);
    return Kind::None;
  }

  // One 4 x 4 colour block (DXT1's, or the colour half of DXT3/5's) into `out` (RGBA, row stride `stride` pixels),
  // clipped to the level's w x h. fourColour: DXT3/5 always use four colours; DXT1 uses three and a transparent black
  // (alpha 0 with 1-bit alpha, opaque black without) when c0 <= c1.
  void colourBlock(const unsigned char * b, bool fourColour, bool oneBitAlpha, unsigned char * out, int stride, int bx, int by,
                   int w, int h, bool writeAlpha)
  {
    const unsigned c0 = u16(b), c1 = u16(b + 2);
    unsigned char pal[4][4];
    rgb565(c0, pal[0][0], pal[0][1], pal[0][2]);
    rgb565(c1, pal[1][0], pal[1][1], pal[1][2]);
    pal[0][3] = pal[1][3] = 255;
    if (fourColour || c0 > c1)
    {
      for (int k = 0; k < 3; k++)
      {
        pal[2][k] = (unsigned char)((2 * pal[0][k] + pal[1][k] + 1) / 3);
        pal[3][k] = (unsigned char)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
      }
      pal[2][3] = pal[3][3] = 255;
    }
    else
    {
      for (int k = 0; k < 3; k++)
      {
        pal[2][k] = (unsigned char)((pal[0][k] + pal[1][k]) / 2);
        pal[3][k] = 0;
      }
      pal[2][3] = 255;
      pal[3][3] = oneBitAlpha ? 0 : 255;
    }
    const unsigned bits = u32(b + 4);
    for (int py = 0; py < 4; py++)
      for (int px = 0; px < 4; px++)
      {
        const int x = bx * 4 + px, y = by * 4 + py;
        if (x >= w || y >= h)
          continue;
        const unsigned i = (bits >> (2 * (py * 4 + px))) & 3;
        unsigned char * o = out + 4 * ((size_t)y * stride + x);
        o[0] = pal[i][0];
        o[1] = pal[i][1];
        o[2] = pal[i][2];
        if (writeAlpha)
          o[3] = pal[i][3];
      }
  }

  bool decodeBlocks(const unsigned char * data, size_t size, Kind kind, bool oneBitAlpha, int w, int h, unsigned char * out,
                    QString & problem)
  {
    const int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
    const size_t blockBytes = kind == Kind::Dxt1 ? 8 : 16;
    if ((size_t)bw * bh * blockBytes > size)
    {
      problem = QString("the level holds %1 bytes, its %2 x %3 blocks need %4").arg(size).arg(bw).arg(bh).arg((size_t)bw * bh * blockBytes);
      return false;
    }
    for (int by = 0; by < bh; by++)
      for (int bx = 0; bx < bw; bx++)
      {
        const unsigned char * b = data + ((size_t)by * bw + bx) * blockBytes;
        if (kind == Kind::Dxt1)
        {
          colourBlock(b, false, oneBitAlpha, out, w, bx, by, w, h, true);
          continue;
        }
        colourBlock(b + 8, true, false, out, w, bx, by, w, h, false);
        unsigned char alpha[16];
        if (kind == Kind::Dxt3)
        {
          for (int k = 0; k < 16; k++)
          {
            const unsigned nib = (b[k / 2] >> (4 * (k & 1))) & 15;
            alpha[k] = (unsigned char)(nib * 17);
          }
        }
        else
        {
          const unsigned a0 = b[0], a1 = b[1];
          unsigned char ap[8];
          ap[0] = (unsigned char)a0;
          ap[1] = (unsigned char)a1;
          if (a0 > a1)
            for (int k = 1; k <= 6; k++)
              ap[1 + k] = (unsigned char)(((7 - k) * a0 + k * a1 + 3) / 7);
          else
          {
            for (int k = 1; k <= 4; k++)
              ap[1 + k] = (unsigned char)(((5 - k) * a0 + k * a1 + 2) / 5);
            ap[6] = 0;
            ap[7] = 255;
          }
          unsigned long long bits = 0;
          for (int k = 0; k < 6; k++)
            bits |= (unsigned long long)b[2 + k] << (8 * k);
          for (int k = 0; k < 16; k++)
            alpha[k] = ap[(bits >> (3 * k)) & 7];
        }
        for (int py = 0; py < 4; py++)
          for (int px = 0; px < 4; px++)
          {
            const int x = bx * 4 + px, y = by * 4 + py;
            if (x < w && y < h)
              out[4 * ((size_t)y * w + x) + 3] = alpha[py * 4 + px];
          }
      }
    return true;
  }
}

bool MapThumbnail::decodeLevel(const QByteArray & bytes, int level, QByteArray & rgba, int & width, int & height, QString & problem)
{
  rgba.clear();
  width = height = 0;
  const unsigned char * p = reinterpret_cast<const unsigned char *>(bytes.constData());
  const BlpInfo info = BlpInfo::fromHeader(p, (size_t)bytes.size(), bytes.size());
  if (!info.headerRead)
  {
    problem = QString("the file is %1 bytes, shorter than a BLP header").arg(bytes.size());
    return false;
  }
  const Kind kind = kindOf(info, problem);
  if (kind == Kind::None)
    return false;
  // BlpInfo checks the mip table of the encodings the app's decoder reads; uncompressed BGRA it does not (intactLevels 0),
  // so its level 0 is taken here and checked against the file below like any level.
  const int levels = info.intactLevels > 0 ? info.intactLevels : (kind == Kind::Bgra && info.mipSizes[0] > 0 ? 1 : 0);
  if (level < 0 || level >= levels || level >= 16)
  {
    problem = QString("level %1 is not one of the file's %2 intact level(s)%3").arg(level).arg(levels)
                .arg(info.mipDamage.isEmpty() ? QString() : " (" + info.mipDamage + ")");
    return false;
  }
  const int w = std::max(1, (int)(info.width >> level)), h = std::max(1, (int)(info.height >> level));
  const size_t offset = info.mipOffsets[level], size = info.mipSizes[level];
  if (offset < (size_t)HeaderSize || offset + size > (size_t)bytes.size())
  {
    problem = QString("level %1 lies outside the file").arg(level);
    return false;
  }
  rgba.resize((int)((size_t)w * h * 4));
  unsigned char * out = reinterpret_cast<unsigned char *>(rgba.data());
  const unsigned char * data = p + offset;
  bool ok = true;
  switch (kind)
  {
    case Kind::Dxt1:
    case Kind::Dxt3:
    case Kind::Dxt5:
      ok = decodeBlocks(data, size, kind, info.alphaDepth == 1, w, h, out, problem);
      break;
    case Kind::Bgra:
      if ((size_t)w * h * 4 > size)
      {
        problem = "the level is shorter than its pixels";
        ok = false;
        break;
      }
      for (size_t i = 0; i < (size_t)w * h; i++)
      {
        out[4 * i] = data[4 * i + 2];
        out[4 * i + 1] = data[4 * i + 1];
        out[4 * i + 2] = data[4 * i];
        out[4 * i + 3] = data[4 * i + 3];
      }
      break;
    case Kind::Palette:
    {
      const size_t n = (size_t)w * h;
      const size_t alphaBytes = info.alphaDepth == 8 ? n : info.alphaDepth == 1 ? (n + 7) / 8 : 0;
      if (bytes.size() < HeaderSize + PaletteSize || n + alphaBytes > size)
      {
        problem = "the palette or the level is shorter than it must be";
        ok = false;
        break;
      }
      const unsigned char * pal = p + HeaderSize;
      for (size_t i = 0; i < n; i++)
      {
        const unsigned char * c = pal + 4 * data[i];
        out[4 * i] = c[2];
        out[4 * i + 1] = c[1];
        out[4 * i + 2] = c[0];
        out[4 * i + 3] = info.alphaDepth == 8 ? data[n + i]
                         : info.alphaDepth == 1 ? (unsigned char)(((data[n + i / 8] >> (i % 8)) & 1) ? 255 : 0) : 255;
      }
      break;
    }
    case Kind::None:
      ok = false;
      break;
  }
  if (!ok)
  {
    rgba.clear();
    return false;
  }
  width = w;
  height = h;
  return true;
}

bool MapThumbnail::fromBlp(const QByteArray & bytes, int side, QByteArray & rgba, QString & problem)
{
  rgba.clear();
  if (side < 8 || side > 256 || (side & (side - 1)) != 0)
  {
    problem = QString("a thumbnail of side %1 is not made").arg(side);
    return false;
  }
  const unsigned char * p = reinterpret_cast<const unsigned char *>(bytes.constData());
  const BlpInfo info = BlpInfo::fromHeader(p, (size_t)bytes.size(), bytes.size());
  if (!info.headerRead || info.width == 0 || info.height == 0)
  {
    problem = info.headerRead ? QString("the image has no pixels") : QString("the file is shorter than a BLP header");
    return false;
  }
  // The smallest intact level still at least side on both axes (level 0 when none is).
  int level = 0;
  for (int l = 1; l < info.intactLevels && l < 16; l++)
    if ((int)(info.width >> l) >= side && (int)(info.height >> l) >= side)
      level = l;
  QByteArray full;
  int w = 0, h = 0;
  if (!decodeLevel(bytes, level, full, w, h, problem))
    return false;
  if (w < side || h < side)
  {
    problem = QString("the image is %1 x %2, smaller than the thumbnail").arg(w).arg(h);
    return false;
  }
  rgba.resize(side * side * 4);
  unsigned char * out = reinterpret_cast<unsigned char *>(rgba.data());
  const unsigned char * in = reinterpret_cast<const unsigned char *>(full.constData());
  for (int ty = 0; ty < side; ty++)
  {
    const int y0 = ty * h / side, y1 = std::max(y0 + 1, (ty + 1) * h / side);
    for (int tx = 0; tx < side; tx++)
    {
      const int x0 = tx * w / side, x1 = std::max(x0 + 1, (tx + 1) * w / side);
      unsigned long sum[4] = { 0, 0, 0, 0 };
      for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
          const unsigned char * s = in + 4 * ((size_t)y * w + x);
          for (int k = 0; k < 4; k++)
            sum[k] += s[k];
        }
      const unsigned long n = (unsigned long)(y1 - y0) * (x1 - x0);
      unsigned char * o = out + 4 * ((size_t)ty * side + tx);
      for (int k = 0; k < 4; k++)
        o[k] = (unsigned char)((sum[k] + n / 2) / n);
    }
  }
  return true;
}
