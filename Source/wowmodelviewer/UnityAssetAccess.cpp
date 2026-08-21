/*
 * UnityAssetAccess.cpp
 */

#include "UnityAssetAccess.h"

#include "ClientProfile.h"
#include "Game.h"
#include "GameFile.h"
#include "GameFolder.h"
#include "HardDriveFile.h"
#include "WoWFolder.h"
#include "logger/Logger.h"

namespace
{
  // Set while ModelViewer::LoadWoW / LoadWoWFromMpq are rebuilding the game folder. The client
  // load yields to the event loop (loading dialog), so an IPC poll can land mid-load; requests
  // are refused cleanly instead of querying a half-built folder.
  int g_clientLoadDepth = 0;
}

UnityAssetAccess::ClientLoadGuard::ClientLoadGuard() { g_clientLoadDepth++; }
UnityAssetAccess::ClientLoadGuard::~ClientLoadGuard() { g_clientLoadDepth--; }

QString UnityAssetAccess::normalizePath(const QString & path)
{
  QString p = path.trimmed().toLower();
  p.replace('\\', '/');
  while (p.startsWith("./"))
    p.remove(0, 2);
  while (p.startsWith('/'))
    p.remove(0, 1);
  return p;
}

bool UnityAssetAccess::hasActiveClient()
{
  // Game::folder() dereferences a pointer that is null until Game::init() ran, and the
  // profile's storage stays Unknown until a client config was actually loaded.
  core::Game & game = core::Game::instance();
  return g_clientLoadDepth == 0 && game.initDone() &&
         game.folder().clientProfile().storage != core::StorageType::Unknown;
}

QString UnityAssetAccess::activeProviderName()
{
  return hasActiveClient() ? GAMEDIRECTORY.clientProfile().storageName() : QStringLiteral("Unknown");
}

namespace
{
  // V1 transport ceiling. The response is base64 inside one JSON line, which transiently costs
  // several times the file size on both ends -- and Qt 5.13's JSON backing silently drops
  // strings past ~128 MB, which would ship ok:true with no data. Real model assets are a few
  // MB; refuse anything larger with a clean error until the binary frame lands.
  const qint64 MAX_ASSET_SIZE = 64 * 1024 * 1024;

  // Copy the whole, RAW file out of a folder-owned GameFile (never ours to delete).
  //
  // Two read modes, because GameFile's memory mode is not always raw: for files whose first
  // chunk magic is known, doPostOpenOperation() indexes the chunks and -- when there is exactly
  // one -- re-points getBuffer()/getSize() at that chunk's payload (8-byte header stripped).
  //   * CASC-addressed files: stream mode (open(false) + read()) goes straight to the storage
  //     read with no chunk handling -> always raw, and a short read reports failure (e.g. an
  //     encrypted file without its key) instead of handing back garbage.
  //   * Everything else (MoPaQ files, custom-directory overrides -- no stream read() there):
  //     memory mode + rawBuffer()/rawSize(), which see past the chunk re-pointing.
  bool readGameFile(GameFile * file, bool preferStream, UnityAssetAccess::Result & r)
  {
    r.path = UnityAssetAccess::normalizePath(file->fullname());
    if (r.fileDataID <= 0 && file->fileDataId() > 0)
      r.fileDataID = file->fileDataId();

    // GameFiles are shared, folder-owned objects: if some other component currently holds this
    // one open, our open() would silently piggyback on its state and our close() would free its
    // buffer under it. Refuse with a clean error instead; the player can simply retry.
    if (file->isCurrentlyOpen())
    {
      r.error = "file is busy in the viewer -- retry";
      return false;
    }

    // Custom-directory overrides (HardDriveFile) have no storage stream read; they take the
    // memory path directly.
    if (dynamic_cast<HardDriveFile *>(file) != nullptr)
      preferStream = false;

    if (preferStream)
    {
      if (!file->open(false))
      {
        r.error = "could not open file in the active client";
        return false;
      }
      const size_t size = file->getSize();
      if ((qint64)size > MAX_ASSET_SIZE)
      {
        file->close();
        r.error = QString("file too large for the V1 base64 transport (%1 bytes)").arg((qulonglong)size);
        return false;
      }
      if (size > 0)
      {
        QByteArray bytes;
        bytes.resize((int)size);
        if ((size_t)bytes.size() != size)
        {
          file->close();
          r.error = "could not allocate the response buffer";
          return false;
        }
        const size_t got = file->read(bytes.data(), size);
        file->close();
        if (got == size)
        {
          r.data = bytes;
          return true;
        }
        if (got > 0)
        {
          r.error = QString("short read (%1 of %2 bytes) -- file may be encrypted or damaged").arg((qulonglong)got).arg((qulonglong)size);
          return false;
        }
        // got == 0: no stream read available for this file after all -> retry below in
        // memory mode (the close above released the stream-mode open).
      }
      else
      {
        file->close();
      }
    }

    if (!file->open(true))
    {
      r.error = "could not open file in the active client";
      return false;
    }
    const unsigned char * raw = file->rawBuffer();
    const size_t rawSize = file->rawSize();
    if (!raw || rawSize == 0 || file->isEof())
    {
      file->close();
      r.error = "file is empty or could not be read";
      return false;
    }
    if ((qint64)rawSize > MAX_ASSET_SIZE)
    {
      file->close();
      r.error = QString("file too large for the V1 base64 transport (%1 bytes)").arg((qulonglong)rawSize);
      return false;
    }
    r.data = QByteArray(reinterpret_cast<const char *>(raw), (int)rawSize);
    file->close();
    return true;
  }
}

UnityAssetAccess::Result UnityAssetAccess::readByPath(const QString & path)
{
  Result r;
  r.path = normalizePath(path);
  r.provider = activeProviderName();

  if (r.path.isEmpty())
  {
    r.error = "empty path";
    return r;
  }
  if (!hasActiveClient())
  {
    r.error = g_clientLoadDepth > 0 ? "game client is still loading" : "no game client loaded";
    return r;
  }

  core::GameFolder & dir = GAMEDIRECTORY; // re-fetched per request: the folder is replaced on client switch
  const bool casc = dir.clientProfile().storage == core::StorageType::CASC;

  // getFile(QString): CASC listfile name map (+ custom files), or on-demand creation from the
  // MoPaQ chain for legacy clients. Returns a folder-owned cached pointer (or nullptr).
  GameFile * file = dir.getFile(r.path);

  // CASC: a listfile path whose file was not present when the name map was built (or that is
  // only known by id) -- resolve the id from the listfile and probe the storage by id.
  if (!file && casc)
  {
    const int id = static_cast<wow::WoWFolder &>(dir).fileID(r.path);
    if (id > 0)
    {
      file = dir.getFile(id);
      if (file)
        r.fileDataID = id;
    }
  }

  if (!file)
  {
    r.error = "not found";
    return r;
  }

  r.ok = readGameFile(file, casc, r);
  return r;
}

UnityAssetAccess::Result UnityAssetAccess::readByFileDataID(int fileDataID)
{
  Result r;
  r.fileDataID = fileDataID;
  r.provider = activeProviderName();

  if (fileDataID <= 0)
  {
    r.error = "invalid fileDataID";
    return r;
  }
  if (!hasActiveClient())
  {
    r.error = g_clientLoadDepth > 0 ? "game client is still loading" : "no game client loaded";
    return r;
  }

  core::GameFolder & dir = GAMEDIRECTORY;
  const core::ClientProfile & prof = dir.clientProfile();
  if (prof.storage != core::StorageType::CASC || prof.lookupMode == core::FileLookupMode::Name)
  {
    // Legacy MPQ clients address files by name only; there is no FileDataID to look up.
    r.error = QString("FileDataID lookup is not supported by the active client (%1, name lookup only)")
                .arg(prof.storageName());
    return r;
  }

  // getFile(int): listfile id map, else a force-open-by-id probe through the active provider.
  GameFile * file = dir.getFile(fileDataID);
  if (!file)
  {
    r.error = "not found";
    return r;
  }

  r.ok = readGameFile(file, true, r);
  return r;
}
