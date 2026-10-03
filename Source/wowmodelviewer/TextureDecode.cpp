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
#include "UnityAssetAccess.h"
#include "video.h"
#include "logger/Logger.h"

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

  TextureDecode::Source finish(const UnityAssetAccess::Result & r, GameFile * file)
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
  const UnityAssetAccess::Result r = UnityAssetAccess::readByPath(entry.path);
  return finish(r, r.ok ? GAMEDIRECTORY.getFile(entry.path) : nullptr);
}

TextureDecode::Source TextureDecode::readId(int fileDataId)
{
  const UnityAssetAccess::Result r = UnityAssetAccess::readByFileDataID(fileDataId);
  // After a successful read the index holds the file under this id: a lookup, no second probe.
  return finish(r, r.ok ? GAMEDIRECTORY.getFile(fileDataId) : nullptr);
}

QString TextureDecode::decoderProblem(bool glReady, const BlpInfo & info)
{
  if (!glReady)
    return "The app's texture decoder is not running yet (its graphics context is not ready).";
  // Without hardware DXT, Texture::load decodes DXT on the CPU (ddslib), whose DXT5 alpha and
  // DXT3/5 colours come out wrong.
  if (info.encoding == 2 && !video.supportCompression)
    return "This graphics driver cannot decode DXT textures, and the app's software fallback gets "
           "their colours and alpha wrong.";
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

  GLint previous = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
  while (glGetError() != GL_NO_ERROR)
  {
  }

  // The app's decoder, Texture::load, run the way TEXTUREMANAGER.add runs it, into a texture of the
  // viewer's own. Not through the manager: it hands back whatever it already holds under the same
  // name -- a model's texture, or one left from a client loaded before this one -- without reading
  // this file.
  GLuint id = 0;
  glGenTextures(1, &id);
  Texture texture(file);
  texture.id = id;
  texture.load();
  glBindTexture(GL_TEXTURE_2D, id);
  p.loadMs = timer.nsecsElapsed() / 1e6;

  auto levelSize = [](int level, GLint & w, GLint & h)
  {
    w = h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &h);
  };

  GLint w0 = 0, h0 = 0, format = 0;
  levelSize(0, w0, h0);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
  // Levels down to 1 x 1 at most: asking for one past that may raise an error, which the read-back
  // check below would then take for its own.
  int possible = 1;
  for (GLint side = (std::max)(w0, h0); side > 1; side /= 2)
    possible++;
  for (int level = 0; level < (std::min)(possible, 16); level++)
  {
    GLint w = 0, h = 0;
    levelSize(level, w, h);
    if (w <= 0 || h <= 0)
      break;
    p.levels++;
  }
  p.width = w0;
  p.height = h0;
  p.internalFormat = format;

  if (w0 <= 0 || h0 <= 0)
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
      GLint w = 0, h = 0;
      levelSize(level + 1, w, h);
      if ((std::max)(w, h) < minSide)
        break;
      level++;
    }
    GLint w = 0, h = 0;
    levelSize(level, w, h);
    // Cleared first, so a read-back that fails can never show what the memory held before.
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(0);
    // Tightly packed rows, as the image holds them, whatever pack state the app left.
    GLint alignment = 4, rowLength = 0, skipRows = 0, skipPixels = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glGetTexImage(GL_TEXTURE_2D, level, GL_BGRA_EXT, GL_UNSIGNED_BYTE, image.bits());
    const GLenum error = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    if (error != GL_NO_ERROR)
      p.error = QString("Reading the decoded texture back failed (graphics error 0x%1).").arg(error, 0, 16);
    else
    {
      p.ok = true;
      p.image = image;
      p.level = level;
    }
  }

  glBindTexture(GL_TEXTURE_2D, (GLuint)previous);
  glDeleteTextures(1, &id);
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
