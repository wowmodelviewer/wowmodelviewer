/*
 * TextureDecode.cpp -- see TextureDecode.h.
 */

#include "TextureDecode.h"

#include <algorithm>

#include <QElapsedTimer>
#include <QImageWriter>
#include <QSaveFile>

#include "Game.h"
#include "GameFile.h"
#include "GameFolder.h"
#include "Texture.h"
#include "TextureCatalog.h"
#include "ClientState.h"
#include "video.h"
#include "logger/Logger.h"

// The OpenGL enums the decoded formats are named by (Texture::sourceFormat).
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

namespace
{
  // The shared reader words its errors for the Unity viewport's requests; say the same in the
  // viewer's terms.
  QString viewerWording(const QString & error)
  {
    if (error.startsWith("file is busy"))
      return "The file is in use elsewhere in the app; select it again in a moment.";
    if (error.startsWith("could not open file"))
      return "The game client could not open this file.";
    if (error.startsWith("file too large"))
      return "The file is larger than the 64 MB the app's file reader accepts.";
    if (error.startsWith("short read"))
      return "The file could not be read completely (" + error.section('(', 1).section(')', 0, 0) +
             "): it may be encrypted, or not downloaded by the game yet.";
    if (error.startsWith("incomplete read"))
      return "The file could not be read completely: the game files may be damaged.";
    if (error.startsWith("file is empty"))
      return "The file is empty or could not be read.";
    if (error == "game client is still loading")
      return "The game client is still loading.";
    if (error == "no game client loaded")
      return "No game client is loaded.";
    if (error == "not found")
      return "The game client has no file with this name or FileDataID.";
    if (error.startsWith("FileDataID lookup is not supported"))
      return "This game client has no FileDataIDs.";
    return error;
  }

  TextureDecode::Source finish(const ClientState::Result & r, GameFile * file)
  {
    TextureDecode::Source s;
    if (!r.ok)
    {
      s.error = viewerWording(r.error);
      return s;
    }
    if (!file)
    {
      s.error = "The game client has no file with this name or FileDataID.";
      return s;
    }
    s.ok = true;
    s.bytes = r.data;
    s.file = file;
    QElapsedTimer check;
    check.start();
    s.info = BlpInfo::fromHeader(reinterpret_cast<const unsigned char *>(r.data.constData()),
                                 (size_t)r.data.size(), r.data.size());
    s.checkMs = check.nsecsElapsed() / 1e6;
    return s;
  }
}

TextureDecode::Source TextureDecode::read(const TextureEntry & entry)
{
  if (entry.unnamed)
    return readId(entry.fileDataId);
  // readByPath resolves the path through the same name map as getFile(QString), so the bytes and
  // the file object decoded below are the same file.
  const ClientState::Result r = ClientState::readByPath(entry.path);
  return finish(r, r.ok ? GAMEDIRECTORY.getFile(entry.path) : nullptr);
}

TextureDecode::Source TextureDecode::readId(int fileDataId)
{
  const ClientState::Result r = ClientState::readByFileDataID(fileDataId);
  // After a successful read the index holds the file under this id: a lookup, no second probe.
  return finish(r, r.ok ? GAMEDIRECTORY.getFile(fileDataId) : nullptr);
}

QString TextureDecode::decoderProblem(bool decoderReady, const BlpInfo &)
{
  // Every BLP encoding is decoded on the CPU (Texture::load), so only an app that has not finished starting
  // has no decoder.
  if (!decoderReady)
    return "The app's texture decoder is not running yet.";
  return QString();
}

TextureDecode::Pixels TextureDecode::decode(GameFile * file, const BlpInfo & info, int minSide)
{
  Pixels p;
  QElapsedTimer timer;
  timer.start();
  if (!file)
  {
    p.error = "The game client has no file with this name or FileDataID.";
    return p;
  }
  // Texture::load opens and closes the file object itself: never under another holder.
  if (file->isCurrentlyOpen())
  {
    p.error = "The file is in use elsewhere in the app; select it again in a moment.";
    return p;
  }

  // The app's decoder, Texture::load, into a texture of the viewer's own. Not through the manager: it hands back
  // whatever it already holds under the same name -- a model's texture, or one left from a client loaded before
  // this one -- without reading this file.
  Texture texture(file);
  texture.load();
  p.loadMs = timer.nsecsElapsed() / 1e6;

  const int w0 = texture.w, h0 = texture.h;
  p.levels = (int)texture.mips.size();
  p.width = w0;
  p.height = h0;
  p.internalFormat = (int)texture.sourceFormat;

  auto levelSize = [&](int level, int & w, int & h) {
    w = std::max(1, w0 >> level);
    h = std::max(1, h0 >> level);
  };

  if (w0 <= 0 || h0 <= 0 || texture.mips.empty())
    p.error = "The texture decoder produced no image.";
  else if ((unsigned)w0 != info.width || (unsigned)h0 != info.height)
    p.error = QString("The texture decoder produced %1 x %2, but the file is %3 x %4.")
                .arg(w0).arg(h0).arg(info.width).arg(info.height);
  else
  {
    int level = 0;
    // Smaller levels only while the file holds them whole (BlpInfo::intactLevels).
    while (minSide > 0 && level + 1 < (std::min)(p.levels, info.intactLevels))
    {
      int w = 0, h = 0;
      levelSize(level + 1, w, h);
      if ((std::max)(w, h) < minSide)
        break;
      level++;
    }
    int w = 0, h = 0;
    levelSize(level, w, h);
    const std::vector<unsigned char> & px = texture.mips[level];
    if (px.size() != (size_t)w * h * 4)
      p.error = "The decoded texture level has the wrong size.";
    else
    {
      // RGBA bytes to QImage's ARGB32 (BGRA in memory).
      QImage image(w, h, QImage::Format_ARGB32);
      for (int y = 0; y < h; y++)
      {
        unsigned char * dst = image.scanLine(y);
        const unsigned char * src = &px[(size_t)y * w * 4];
        for (int x = 0; x < w; x++, src += 4, dst += 4)
        {
          dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
        }
      }
      p.ok = true;
      p.image = image;
      p.level = level;
    }
  }

  p.ms = timer.nsecsElapsed() / 1e6;
  return p;
}

QString TextureDecode::decodedFormatName(int internalFormat, const BlpInfo & info)
{
  switch (internalFormat)
  {
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:  return "DXT1";
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: return "DXT1 with 1-bit alpha";
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: return "DXT3";
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return "DXT5";
    default: break;
  }
  if (info.encoding == 1)
    return info.alphaDepth ? QString("Palettised with %1-bit alpha").arg(info.alphaDepth) : QString("Palettised");
  return QString("Format 0x%1").arg(internalFormat, 0, 16);
}

TextureDecode::Alpha TextureDecode::alphaOf(const QImage & argb32)
{
  Alpha a;
  for (int y = 0; y < argb32.height(); y++)
  {
    const QRgb * row = reinterpret_cast<const QRgb *>(argb32.constScanLine(y));
    for (int x = 0; x < argb32.width(); x++)
    {
      const int alpha = qAlpha(row[x]);
      if (alpha == 0)
        a.transparent++;
      else if (alpha < 255)
        a.partial++;
    }
  }
  a.total = (qint64)argb32.width() * argb32.height();
  a.present = a.transparent + a.partial > 0;
  return a;
}

bool TextureDecode::alphaOfLevel0(const QByteArray & bytes, const BlpInfo & info, int internalFormat, Alpha & out)
{
  out = Alpha();
  const qint64 w = info.width, h = info.height;
  if (!info.supported || w <= 0 || h <= 0 || info.mipLevels < 1)
    return false;
  const qint64 offset = info.mipOffsets[0], size = info.mipSizes[0];
  if (offset <= 0 || size <= 0 || offset + size > bytes.size())
    return false;
  const unsigned char * d = reinterpret_cast<const unsigned char *>(bytes.constData()) + offset;
  auto count = [&out](int a) {
    if (a == 0)
      out.transparent++;
    else if (a < 255)
      out.partial++;
  };

  if (info.encoding == 1)
  {
    // Palette indices, one byte per pixel, then the alpha: bytes (8), or bits, lowest first (1).
    const qint64 pixels = w * h;
    if (info.alphaDepth == 8)
    {
      if (size < 2 * pixels)
        return false;
      for (qint64 i = 0; i < pixels; i++)
        count(d[pixels + i]);
    }
    else if (info.alphaDepth == 1)
    {
      if (size < pixels + (pixels + 7) / 8)
        return false;
      for (qint64 i = 0; i < pixels; i++)
        count(((d[pixels + (i >> 3)] >> (i & 7)) & 1) ? 255 : 0);
    }
    else if (info.alphaDepth != 0)
      return false;
  }
  else if (info.encoding == 2)
  {
    const qint64 blocksX = (w + 3) / 4, blocksY = (h + 3) / 4;
    const int blockSize = (internalFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) ? 8 : 16;
    if (internalFormat != GL_COMPRESSED_RGB_S3TC_DXT1_EXT && internalFormat != GL_COMPRESSED_RGBA_S3TC_DXT1_EXT &&
        internalFormat != GL_COMPRESSED_RGBA_S3TC_DXT3_EXT && internalFormat != GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)
      return false;
    if (size < blocksX * blocksY * blockSize)
      return false;
    if (internalFormat != GL_COMPRESSED_RGB_S3TC_DXT1_EXT)   // DXT1 without alpha: every pixel opaque
      for (qint64 by = 0; by < blocksY; by++)
        for (qint64 bx = 0; bx < blocksX; bx++)
        {
          const unsigned char * b = d + (by * blocksX + bx) * blockSize;
          const int rows = (int)std::min<qint64>(4, h - by * 4), cols = (int)std::min<qint64>(4, w - bx * 4);
          if (internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)
          {
            // Colour index 3 is transparent in a block whose first colour is not above the second.
            const unsigned c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
            const unsigned bits = b[4] | (b[5] << 8) | (b[6] << 16) | ((unsigned)b[7] << 24);
            for (int y = 0; y < rows; y++)
              for (int x = 0; x < cols; x++)
                count(c0 <= c1 && ((bits >> (2 * (y * 4 + x))) & 3) == 3 ? 0 : 255);
          }
          else if (internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT)
          {
            // Four bits per pixel, a row per 16-bit word, expanded by 17 (0 -> 0, 15 -> 255).
            for (int y = 0; y < rows; y++)
            {
              const unsigned word = b[2 * y] | (b[2 * y + 1] << 8);
              for (int x = 0; x < cols; x++)
                count(((word >> (4 * x)) & 15) * 17);
            }
          }
          else
          {
            // Two end points and a 3-bit index per pixel into eight values.
            const int a0 = b[0], a1 = b[1];
            int value[8] = { a0, a1, 0, 0, 0, 0, 0, 255 };
            if (a0 > a1)
              for (int i = 2; i < 8; i++)
                value[i] = ((8 - i) * a0 + (i - 1) * a1 + 3) / 7;
            else
            {
              for (int i = 2; i < 6; i++)
                value[i] = ((6 - i) * a0 + (i - 1) * a1 + 2) / 5;
              value[6] = 0;
              value[7] = 255;
            }
            unsigned long long bits = 0;
            for (int i = 0; i < 6; i++)
              bits |= (unsigned long long)b[2 + i] << (8 * i);
            for (int y = 0; y < rows; y++)
              for (int x = 0; x < cols; x++)
                count(value[(bits >> (3 * (y * 4 + x))) & 7]);
          }
        }
  }
  else
    return false;
  out.total = w * h;
  out.present = out.transparent + out.partial > 0;
  return true;
}

bool TextureDecode::saveBlp(const QString & target, const QByteArray & bytes, QString & error)
{
  QSaveFile out(target);
  if (!out.open(QIODevice::WriteOnly))
  {
    error = out.errorString();
    return false;
  }
  if (out.write(bytes) != bytes.size())
  {
    error = out.errorString();
    out.cancelWriting();
    return false;
  }
  if (!out.commit())
  {
    error = out.errorString();
    return false;
  }
  return true;
}

bool TextureDecode::savePng(const QString & target, const QImage & image, QString & error)
{
  QImageWriter writer(target, "png");
  if (!writer.write(image))
  {
    error = writer.errorString();
    return false;
  }
  return true;
}
