/*
 * TextureDecode.h
 *
 * Reading, checking, decoding and saving one BLP texture for the Texture Viewer, with the app's
 * own pieces only:
 *
 *  - the bytes come from UnityAssetAccess::readByPath / readByFileDataID, the reader the Unity
 *    viewport is served from: it refuses a file another part of the app holds open and reports a
 *    file it cannot read completely (encrypted, not downloaded);
 *  - BlpInfo checks the header and mip table against the bytes, so the decoder is only ever given
 *    a texture it reads correctly and safely (it trusts every header field);
 *  - the pixels come from the app's texture decoder (Texture::load, which needs the hidden
 *    canvas's GL context), run into a texture of the viewer's own -- never one the texture manager
 *    already holds under that name -- read back as the decoder left them, and released at once;
 *  - PNG through QImage, the original BLP as the bytes read.
 *
 * GUI thread only.
 */

#ifndef TEXTUREDECODE_H
#define TEXTUREDECODE_H

#include <QByteArray>
#include <QImage>
#include <QString>

#include "BlpInfo.h"

class GameFile;
struct TextureEntry;

namespace TextureDecode
{
  struct Source
  {
    bool ok = false;
    QString error;           // for the viewer, when !ok
    QByteArray bytes;        // the whole file
    GameFile * file = nullptr;
    BlpInfo info;            // from the bytes (valid when ok)
    double checkMs = 0;      // of that: the header and mip table check
  };

  // The texture's bytes and header, from the index entry (by path, or by FileDataID for files the
  // index knows only by id).
  Source read(const TextureEntry & entry);
  // The same for a FileDataID the list does not have: the index looks it up in the client storage
  // (and keeps the file it finds, as it does for every id the app opens).
  Source readId(int fileDataId);

  struct Pixels
  {
    bool ok = false;
    QString error;
    QImage image;            // Format_ARGB32, straight alpha, of the level read
    int level = 0;           // mip level read
    int width = 0;           // of level 0, as decoded
    int height = 0;
    int levels = 0;          // mip levels the decoder uploaded
    int internalFormat = 0;  // GL internal format of level 0, as decoded
    double ms = 0;           // decode + read back
    double loadMs = 0;       // of that: the decoder (its own read of the file, and the upload of every level)
  };

  // Why the texture decoder cannot run now ("" when it can): no GL context yet, or the graphics
  // driver lacks hardware DXT decoding for a DXT texture (the software fallback is known to be
  // wrong). 'glReady' is the canvas check the app uses (canvas && video.render && canvas->init).
  QString decoderProblem(bool glReady, const BlpInfo & info);

  // Decode through the app's decoder and read back the smallest mip level whose longer side is at
  // least minSide (0 = level 0, full resolution), among the file's intact levels. Only call with a
  // supported BlpInfo.
  Pixels decode(GameFile * file, const BlpInfo & info, int minSide);

  // "DXT5", "DXT1", "RGBA (palettised)" ... from the GL internal format the decoder uploaded.
  QString decodedFormatName(int internalFormat, const BlpInfo & info);

  struct Alpha
  {
    bool present = false;   // some pixel is not fully opaque
    qint64 transparent = 0; // alpha 0
    qint64 partial = 0;     // 0 < alpha < 255
    qint64 total = 0;
  };
  Alpha alphaOf(const QImage & argb32);
  // The same for level 0, from the file's own bytes, read the way the decoder gives it: palettised alpha
  // as Texture::load expands it (bytes, or bits low bit first), DXT alpha as the format it uploads
  // decodes ('internalFormat'; DXT5's in-between values rounded to nearest). For a texture previewed from
  // a smaller level, whose own pixels would not do: filtering turns cut-out edges into partial alpha.
  // False for a kind or a size it does not read.
  bool alphaOfLevel0(const QByteArray & bytes, const BlpInfo & info, int internalFormat, Alpha & out);

  bool saveBlp(const QString & target, const QByteArray & bytes, QString & error);
  bool savePng(const QString & target, const QImage & image, QString & error);
}

#endif // TEXTUREDECODE_H
