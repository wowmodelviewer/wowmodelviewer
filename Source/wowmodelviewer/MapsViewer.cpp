/*
 * MapsViewer.cpp -- the Maps viewer's state (MapsViewer.h; docs/world-viewer/05-maps-viewer-ui.md).
 */

#include "MapsViewer.h"

#include <algorithm>
#include <cmath>

#include "Game.h"
#include "UnityAssetAccess.h"
#include "UnityRendererHost.h"
#include "WoWFolder.h"
#include "WorldMapCatalog.h"
#include "modelviewer.h"

#include "logger/Logger.h"

namespace
{
  QString tileName(int x, int y) { return QString("%1_%2").arg(x).arg(y); }
  double msOf(const QElapsedTimer & t) { return t.isValid() ? t.nsecsElapsed() / 1e6 : -1.0; }

  // A tile's picture.
  enum MinimapState : unsigned char
  {
    MinimapNone = 0,    // the WDT names no minimap for it (or it is not part of the map)
    MinimapQueued,      // to be read
    MinimapReading,     // with the worker
    MinimapLoaded,      // its thumbnail came
    MinimapMissing,     // it has none: not in the client's files, not downloaded, unreadable (the reason is kept)
  };

  // The largest minimap file read: a mebibyte, room for a 1024 x 1024 DXT1 image with its levels (the audit's minimaps are
  // 512 x 512 and 256 x 256 DXT1 without levels, 00-audit.md 4.6). A larger file is not read: its tile shows no picture,
  // and the hover line says why.
  const qint64 MaxMinimapBytes = 1 << 20;
  const QByteArray NoThumbnail;
}

bool MapsViewer::tileOf(double x, double y, int & tx, int & ty)
{
  tx = ty = -1;
  if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > 1e6 || std::fabs(y) > 1e6)
    return false;
  tx = (int)std::floor(32.0 - y / TileSize);
  ty = (int)std::floor(32.0 - x / TileSize);
  return tx >= 0 && tx < Tiles && ty >= 0 && ty < Tiles;
}

void MapsViewer::pointIn(int tx, int ty, double fx, double fy, double & x, double & y)
{
  x = (32.0 - ty - fy) * TileSize;
  y = (32.0 - tx - fx) * TileSize;
}

const std::vector<MapsViewer::DisplayMode> & MapsViewer::displayModes()
{
  // The player's views (WmvTerrainDebug.ViewNames), all of them but the plain-weights comparison (a validation fixture
  // that fetches textures of its own). "auto" is stage 04's default rule.
  static const std::vector<DisplayMode> modes = {
    { "Textured", "auto", "the tile's textures; chunks the material cannot draw are drawn in neutral shading" },
    { "Textured, diagnostic light", "texturedlit", "the tile's textures under the viewer's diagnostic light" },
    { "Neutral", "shaded", "neutral grey shading of the geometry" },
    { "Chunk borders", "chunks", "neutral shading tinted per chunk, so the 16 x 16 chunks show" },
    { "Wireframe", "wire", "every triangle's edges" },
    { "Neutral with wireframe", "shadedwire", "neutral shading with the triangle edges over it" },
    { "Normals", "normals", "the surface normal as colour" },
    { "Material states", "materials", "each chunk in its material state's colour (textured, unsupported, missing ...)" },
  };
  return modes;
}

MapsViewer::MapsViewer(ModelViewer * viewer) : m_viewer(viewer)
{
  m_state.assign(Tiles * Tiles, (unsigned char)TileState::NotInWdt);
  m_minimapTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { pumpMinimaps(); });
}

UnityIpcServer * MapsViewer::ipc() const
{
  return m_viewer && m_viewer->unityRendererHost ? m_viewer->unityRendererHost->ipc() : nullptr;
}

void MapsViewer::changed()
{
  if (onChanged)
    onChanged();
}

bool MapsViewer::clientCurrent(int epoch, quint64 generation) const
{
  if (!m_viewer || m_viewer->m_clientLoading || UnityAssetAccess::isClientLoading())
    return false;
  const std::shared_ptr<wow::WorldMapCatalog> catalog = m_viewer->mapCatalog();
  return catalog && catalog->belongsToActiveClient() && epoch == UnityAssetAccess::clientEpoch() &&
         generation == catalog->client().storageGeneration;
}

// ---------------------------------------------------------------- the viewer mode and the client

void MapsViewer::Entered()
{
  m_active = true;
  // The view the user chose, again, for a player that was restarted while another viewer was on.
  UnityIpcServer * s = ipc();
  const QString view = displayModes()[m_displayMode].view;
  if (s && s->playerInspectsMapTiles() && m_viewSent != view && !(m_viewSent.isEmpty() && view == "auto"))
  {
    s->sendSetTerrainView(view);
    m_viewSent = view;
  }
  LOG_INFO << "[maps] entered" << (m_mapId >= 0 ? QString("with map %1 picked").arg(m_mapId) : QString("with no map picked"));
  pumpMinimaps();
  changed();
}

void MapsViewer::Left()
{
  m_active = false;
  // The terrain belongs to this viewer: leaving it takes the tile away, and a load still being built with it (the
  // player's model view comes back as it was). Always -- whatever this viewer's own phase says (a load can still be in
  // flight behind an unavailable tile picked after it): clearTerrain to a player with nothing does nothing.
  const bool had = m_load != 0 || m_shownX >= 0 || m_tilePhase == TilePhase::Loading || m_tilePhase == TilePhase::Loaded;
  m_viewer->ClearTerrain("the Maps viewer was left");
  if (had)
  {
    m_tilePhase = TilePhase::Cleared;
    m_tileDetail = "The world was taken away when the Maps viewer was left. Enter world to load it again.";
  }
  m_load = 0;
  m_shownX = m_shownY = m_shownMap = -1;
  m_shownView.clear();
  leaveWorld();
  LOG_INFO << "[maps] left; the terrain cleared" << (minimapsLoading() ? "; the minimap pictures wait for the viewer's return" : "");
  changed();
}

void MapsViewer::ClientReplaced()
{
  // Everything about the old client goes now: its reads are cancelled by the server (cancelMapReads), its answers are
  // dropped by their epoch and serial, and the terrain was cleared by ResetClientState.
  if (UnityIpcServer * s = ipc())
    s->cancelViewerReads();
  resetMap();
  resetTile();
  m_shownX = m_shownY = m_shownMap = -1;
  m_shownView.clear();
  leaveWorld();
  m_mapDetail = "A client is being loaded.";
  LOG_INFO << "[maps] the client is being replaced: the map, its tiles and the terrain forgotten";
  changed();
}

void MapsViewer::ClientLoaded()
{
  // Nothing picked before the load is the new client's (a pick made while it loaded was refused): start clean.
  resetMap();
  resetTile();
  m_shownX = m_shownY = m_shownMap = -1;
  m_shownView.clear();
  leaveWorld();
  changed();
}

void MapsViewer::resetMap()
{
  stopMinimaps();
  if (m_readTicket != 0)
    if (UnityIpcServer * s = ipc())
      s->cancelViewerReads();
  m_readTicket = 0;
  m_readDeferred = false;   // (a later pick defers again if it must; a pick without a WDT read must not inherit it)
  m_mapId = -1;
  m_mapName.clear();
  m_mapDirectory.clear();
  m_mapPhase = MapPhase::None;
  m_mapDetail.clear();
  m_wdtDescription.clear();
  m_inspection = 0;
  m_wdtBytes.clear();
  m_wdtMd5.clear();
  m_wdtFileDataID = 0;
  m_state.assign(Tiles * Tiles, (unsigned char)TileState::NotInWdt);
  m_roots.clear();
  m_tex0s.clear();
  m_tilesInWdt = 0;
  m_tilesAvailable = 0;
  m_minimapIds.clear();
  m_thumbs.clear();
  m_thumbState.clear();
  m_thumbReason.clear();
  m_minimapsWanted = m_minimapsLoaded = m_minimapsMissing = 0;
  m_minimapsMs = -1.0;
  m_imagesSerial++;
  m_globalWmoRecords = 0;
  m_globalWmoFileDataID = 0;
  m_globalWmoPath.clear();
  m_hasTarget = false;
  m_targetHasZ = false;
}

void MapsViewer::resetTile()
{
  m_tilePhase = TilePhase::None;
  m_tileX = m_tileY = -1;
  m_tileDetail.clear();
  m_load = 0;
}

// ---------------------------------------------------------------- a map picked

void MapsViewer::SelectMap(int mapId)
{
  QElapsedTimer clock;
  clock.start();
  if (mapId == m_mapId && m_mapPhase != MapPhase::Failed && m_mapPhase != MapPhase::None &&
      m_mapPhase != MapPhase::WaitingPlayer)
    return;   // picked again: nothing to do (a failed one, or one waiting for the player, is read again)
  // Another map: the terrain of the one before goes with it -- the tile on screen and a load still being built --, so
  // the viewport only ever shows a tile of the map whose grid is shown.
  if (mapId != m_mapId && (m_load != 0 || m_shownX >= 0 || m_tilePhase == TilePhase::Loading))
  {
    m_viewer->ClearTerrain("another map was picked");
    m_shownX = m_shownY = m_shownMap = -1;
    m_shownView.clear();
  }
  if (mapId != m_mapId)
    leaveWorld();
  resetMap();
  resetTile();
  m_pickClock.start();
  m_mapId = mapId;
  const std::shared_ptr<wow::WorldMapCatalog> catalog = m_viewer->mapCatalog();
  if (m_viewer->m_clientLoading || UnityAssetAccess::isClientLoading() || !UnityAssetAccess::hasActiveClient() || !catalog ||
      catalog->status() != wow::WorldMapCatalog::Status::Ready || !catalog->belongsToActiveClient())
  {
    m_mapPhase = MapPhase::Failed;
    m_mapDetail = catalog && catalog->status() != wow::WorldMapCatalog::Status::Ready
                    ? "This client's maps cannot be listed: " + wow::WorldMapCatalog::statusName(catalog->status()) +
                        (catalog->statusDetail().isEmpty() ? QString() : " (" + catalog->statusDetail() + ")")
                    : QString("No client is loaded, or one is being loaded.");
    changed();
    return;
  }
  const wow::MapEntry * e = catalog->find(mapId);
  if (!e)
  {
    m_mapPhase = MapPhase::Failed;
    m_mapDetail = QString("Map %1 is not in this client's Map.db2.").arg(mapId);
    changed();
    return;
  }
  m_mapName = e->name;
  m_mapDirectory = e->directory;
  m_epoch = UnityAssetAccess::clientEpoch();
  m_generation = catalog->client().storageGeneration;
  switch (e->wdt.kind)
  {
    case wow::WdtReference::Kind::FileDataId:
      m_wdtDescription = QString("WDT FileDataID %1 (Map.db2's)").arg(e->wdt.fileDataId);
      break;
    case wow::WdtReference::Kind::Path:
      m_wdtDescription = "WDT " + e->wdt.path + " (by the directory, found through the storage root's names)";
      break;
    case wow::WdtReference::Kind::Unresolved:
      m_wdtDescription = "no WDT";
      m_mapPhase = MapPhase::NoWdt;
      m_mapDetail = "This client gives the map no WDT file: " + e->wdt.why;
      m_timings.selectMs = msOf(clock);
      LOG_INFO << "[maps] map" << mapId << e->directory << "picked: no WDT --" << e->wdt.why;
      changed();
      return;
  }
  beginRead();
  m_timings.selectMs = msOf(clock);
  LOG_INFO << "[maps] map" << mapId << e->directory << "picked:" << m_wdtDescription << "-- read by the map asset worker"
           << "(inspection" << m_inspection << "); the pick took" << m_timings.selectMs << "ms on this thread";
  changed();
}

void MapsViewer::beginRead()
{
  const std::shared_ptr<wow::WorldMapCatalog> catalog = m_viewer->mapCatalog();
  const wow::MapEntry * e = catalog ? catalog->find(m_mapId) : nullptr;
  UnityIpcServer * s = ipc();
  const auto fail = [this](const QString & why) {
    m_mapPhase = MapPhase::Failed;
    m_mapDetail = why;
    LOG_WARNING << "[maps] map" << m_mapId << ": the WDT is not read --" << why;
  };
  if (!e)
    return fail("the map is no longer in the catalogue");
  if (!s || !m_viewer->m_mapAssetWorker || m_viewer->m_mapAssetWorker->stopped())
    return fail("The map asset worker is not running.");
  // The worker's results are taken back by the IPC server's polls, which run only while it listens for the player:
  // without one nothing would come back (and every pick would leave a result held by the worker). The read waits for
  // the player (OnPlayerReady), as the tile table does.
  if (!s->isListening())
  {
    m_mapPhase = MapPhase::WaitingPlayer;
    m_mapDetail = "Waiting for the Unity viewport: the map's tiles are read with it.";
    return;
  }
  if (!m_viewer->EnsureMapAssetLease(m_epoch, m_generation))
    return fail("The client's storage is not open.");
  // At most one read of this viewer's with the worker: a pick made while an earlier one is still out waits for it
  // to come back (cancelled, it comes back at once unless its read began), then is read (OnWdtRead).
  if (s->viewerReadsInFlight() > 0)
  {
    s->cancelViewerReads();
    m_readDeferred = true;
    m_mapPhase = MapPhase::Reading;
    m_mapDetail = "Reading the map's WDT.";
    return;
  }
  m_readDeferred = false;
  m_inspection = ++m_inspectionSerial;
  MapAssetWorker::Job job;
  if (e->wdt.kind == wow::WdtReference::Kind::FileDataId)
    job.fileDataID = e->wdt.fileDataId;
  else
    job.path = e->wdt.path;
  job.maxBytes = UnityIpcServer::MAX_MAP_ASSET_BYTES;
  job.epoch = m_epoch;
  job.generation = m_generation;
  job.role = "mapsWdt";
  s->cancelViewerReads();   // a read of an earlier pick still in flight: not wanted any more
  if (!s->submitViewerRead(job))
    return fail("The map asset worker refused the read.");
  m_readTicket = job.ticket;
  m_mapPhase = MapPhase::Reading;
  m_mapDetail = "Reading the map's WDT.";
}

void MapsViewer::readDeferredWdt()
{
  // The pick that waited for this viewer's earlier reads to come back is read now.
  UnityIpcServer * s = ipc();
  if (m_readDeferred && m_mapId >= 0 && s && s->viewerReadsInFlight() == 0)
  {
    beginRead();
    changed();
  }
}

void MapsViewer::OnWdtRead(MapAssetWorker::Result & result)
{
  // A minimap picture of the map's (the viewer's other reads).
  const auto mt = m_minimapTickets.find(result.ticket);
  if (result.ticket != 0 && mt != m_minimapTickets.end())
  {
    const int index = mt->second;
    m_minimapTickets.erase(mt);
    onMinimapRead(index, result);
    readDeferredWdt();
    return;
  }
  if (result.ticket == 0 || result.ticket != m_readTicket)
  {
    LOG_INFO << "[maps] a" << (result.role == "minimap" ? "minimap" : "WDT") << "read (ticket" << (qulonglong)result.ticket
             << ") of an earlier pick came back: dropped";
    readDeferredWdt();
    return;
  }
  QElapsedTimer clock;
  clock.start();
  m_readTicket = 0;
  m_timings.wdtWaitMs = result.waitMs;
  m_timings.wdtReadMs = result.workMs;
  if (!clientCurrent(m_epoch, m_generation))
  {
    LOG_INFO << "[maps] the WDT read of map" << m_mapId << "came back for another client (epoch" << m_epoch << "): dropped";
    return;
  }
  if (result.outcome != MapAssetWorker::Result::Outcome::Read)
  {
    m_mapPhase = MapPhase::Failed;
    m_mapDetail = "The WDT was not read (" + MapAssetWorker::outcomeName(result.outcome) + "): " + result.detail;
    changed();
    return;
  }
  const std::shared_ptr<wow::WorldMapCatalog> catalog = m_viewer->mapCatalog();
  catalog->noteInspection(m_mapId, result.inspection);
  const wow::WdtInspection & i = result.inspection;
  if (i.state != wow::WdtAvailability::Available)
  {
    m_mapPhase = MapPhase::Failed;
    m_mapDetail = "The map's WDT is " + wow::WorldMapCatalog::availabilityName(i.state) + ": " + i.detail;
    LOG_INFO << "[maps] map" << m_mapId << ":" << m_mapDetail;
    changed();
    return;
  }
  const wow::MapEntry * e = catalog->find(m_mapId);
  m_wdtFileDataID = e && e->wdt.kind == wow::WdtReference::Kind::FileDataId ? e->wdt.fileDataId : i.storageFileDataId;
  m_wdtBytes = result.bytes;
  m_wdtMd5 = i.contentKey;
  LOG_INFO << "[maps] map" << m_mapId << ": WDT" << m_wdtFileDataID << "read by the worker," << i.bytes << "bytes, content key"
           << i.contentKey << "checked; waited" << result.waitMs << "ms, read" << result.workMs << "ms";
  sendInspection();
  m_timings.wdtResultMs = msOf(clock);
  changed();
}

void MapsViewer::sendInspection()
{
  UnityIpcServer * s = ipc();
  if (!s || !s->isUnityReady())
  {
    m_mapPhase = MapPhase::WaitingPlayer;
    m_mapDetail = "Waiting for the Unity viewport to read the map's tiles.";
    return;
  }
  if (!s->playerInspectsMapTiles())
  {
    m_mapPhase = MapPhase::WaitingPlayer;
    m_mapDetail = QString("The Unity renderer build in use speaks protocol %1; listing a map's tiles needs protocol 14. "
                          "Rebuild the player from Tools\\UnityRendererProject.").arg(s->playerProtocolVersion());
    return;
  }
  if (!s->sendInspectMapTiles(m_inspection, m_mapId, m_wdtFileDataID, m_epoch, m_wdtMd5, m_wdtBytes))
  {
    m_mapPhase = MapPhase::Failed;
    m_mapDetail = "The tile table could not be asked for: the Unity viewport is not reading what it is sent.";
    return;
  }
  m_inspectClock.start();
  m_mapPhase = MapPhase::Inspecting;
  m_mapDetail = "Reading the map's tiles.";
}

void MapsViewer::OnPlayerReady()
{
  m_viewSent.clear();
  const QString view = displayModes()[m_displayMode].view;
  UnityIpcServer * s = ipc();
  if (s && s->playerInspectsMapTiles() && view != "auto")
  {
    s->sendSetTerrainView(view);
    m_viewSent = view;
  }
  // A new player has no tile, and has not seen the table asked of the one before.
  if (m_tilePhase == TilePhase::Loading || m_tilePhase == TilePhase::Loaded)
  {
    m_tilePhase = TilePhase::Cleared;
    m_tileDetail = "The Unity viewport was restarted. Enter world to load the world again.";
  }
  m_load = 0;
  m_shownX = m_shownY = m_shownMap = -1;
  leaveWorld();
  if ((m_mapPhase == MapPhase::WaitingPlayer || m_mapPhase == MapPhase::Inspecting) && !m_wdtBytes.isEmpty())
    sendInspection();
  else if (m_mapPhase == MapPhase::WaitingPlayer && m_mapId >= 0 && m_readTicket == 0)
    beginRead();   // the WDT waited for a server to take its read back
  pumpMinimaps();  // a load in flight held the pictures back, and the restart ended it without a report
  changed();
}

void MapsViewer::OnTilesInspected(const UnityIpcServer::MapTilesInspection & r)
{
  if (r.inspection != m_inspection || m_mapPhase != MapPhase::Inspecting)
  {
    LOG_INFO << "[maps] a tile table (inspection" << r.inspection << ") of an earlier pick came back: dropped";
    return;
  }
  if (r.mapId != m_mapId || r.assetEpoch != m_epoch || !clientCurrent(m_epoch, m_generation))
  {
    LOG_INFO << "[maps] a tile table for map" << r.mapId << "epoch" << r.assetEpoch << "came back; map" << m_mapId << "epoch"
             << m_epoch << "is picked: dropped";
    return;
  }
  m_timings.inspectRoundTripMs = msOf(m_inspectClock);
  m_timings.inspectParseMs = r.parseMs;
  m_wdtBytes.clear();
  if (r.status != "inspected" || !r.tableValid)
  {
    m_mapPhase = MapPhase::Failed;
    m_mapDetail = "The map's tile table could not be read: " + (r.status != "inspected" ? r.reason : r.tableProblem);
    changed();
    return;
  }
  buildGrid(r);
  changed();
}

void MapsViewer::buildGrid(const UnityIpcServer::MapTilesInspection & r)
{
  QElapsedTimer clock;
  clock.start();
  m_state.assign(Tiles * Tiles, (unsigned char)TileState::NotInWdt);
  m_roots = r.rootIds.empty() ? std::vector<quint32>(Tiles * Tiles, 0) : r.rootIds;
  m_tex0s = r.tex0Ids.empty() ? std::vector<quint32>(Tiles * Tiles, 0) : r.tex0Ids;
  m_tilesInWdt = 0;
  m_tilesAvailable = 0;
  // Each root ADT the WDT names, asked of the storage root: listed, on this computer. Nothing is read.
  wow::WoWFolder & folder = static_cast<wow::WoWFolder &>(GAMEDIRECTORY);
  for (int i = 0; i < Tiles * Tiles; i++)
  {
    const unsigned char f = (unsigned char)r.tileFlags[i];
    if (!(f & 1))
      continue;
    m_tilesInWdt++;
    TileState s = TileState::NoRoot;
    if ((f & 2) && m_roots[i] != 0)
    {
      const CASCFolder::RootEntry root = folder.storageRootEntry((int)m_roots[i]);
      s = !root.listed ? TileState::NotInStorage : !root.local ? TileState::NotLocal : TileState::Available;
    }
    if (s == TileState::Available)
      m_tilesAvailable++;
    m_state[i] = (unsigned char)s;
  }
  m_timings.gridMs = msOf(clock);
  m_timings.pickToGridMs = msOf(m_pickClock);
  m_mapPhase = MapPhase::Ready;
  m_globalWmoRecords = r.globalWmoRecords;
  m_globalWmoFileDataID = r.globalWmoFileDataID;
  m_globalWmoPath = r.globalWmoPath;
  if (r.kind == "GlobalWmo")
    m_mapDetail = QString("This map has no terrain tiles: it is one world model (%1). Entering a world model in the world view "
                          "is a later stage (world models in the world, docs/world-viewer/roadmap.md); it can be viewed in "
                          "Buildings.").arg(m_globalWmoFileDataID ? QString("WMO FileDataID %1").arg(m_globalWmoFileDataID)
                                                                 : !m_globalWmoPath.isEmpty() ? m_globalWmoPath
                                                                                              : QString("its file is not named"));
  else if (r.kind == "Empty" || m_tilesInWdt == 0)
    m_mapDetail = "This map's WDT names no terrain tile.";
  else if (m_tilesAvailable == 0)
    m_mapDetail = QString("None of the %1 tiles can be loaded on this computer.").arg(m_tilesInWdt);
  else
    m_mapDetail.clear();
  LOG_INFO << "[maps] map" << m_mapId << m_mapDirectory << ": tile grid --" << m_tilesInWdt << "tile(s) in the WDT,"
           << m_tilesAvailable << "available," << tilesCount(TileState::NoRoot) << "without a root ADT,"
           << tilesCount(TileState::NotInStorage) << "not in the client's storage," << tilesCount(TileState::NotLocal)
           << "not on this computer; kind" << r.kind << "; the table took" << m_timings.gridMs << "ms on this thread, "
           << m_timings.pickToGridMs << "ms from the pick (player parse" << r.parseMs << "ms, round trip"
           << m_timings.inspectRoundTripMs << "ms)";
  startMinimaps(r);
}

// ---------------------------------------------------------------- the overhead map's pictures

void MapsViewer::startMinimaps(const UnityIpcServer::MapTilesInspection & r)
{
  m_minimapIds = r.minimapIds;
  m_thumbs.assign(Tiles * Tiles, QByteArray());
  m_thumbState.assign(Tiles * Tiles, MinimapNone);
  m_thumbReason.assign(Tiles * Tiles, QString());
  m_minimapQueue.clear();
  m_minimapsWanted = m_minimapsLoaded = m_minimapsMissing = 0;
  m_minimapsMs = -1.0;
  m_imagesSerial++;
  if (m_minimapIds.size() != (size_t)(Tiles * Tiles))
  {
    LOG_INFO << "[maps] map" << m_mapId << ": no minimap pictures (the table names none:"
             << (r.hasMaid ? "a player before protocol 15" : "the WDT has no MAID") << ")";
    return;
  }
  // Each tile's minimap, asked of the storage root first (listed, on this computer: nothing read), as the root ADTs are.
  wow::WoWFolder & folder = static_cast<wow::WoWFolder &>(GAMEDIRECTORY);
  int minX, minY, maxX, maxY;
  const bool box = tileExtent(minX, minY, maxX, maxY);
  const double cx = box ? (minX + maxX) / 2.0 : 32.0, cy = box ? (minY + maxY) / 2.0 : 32.0;
  std::vector<std::pair<double, int>> order;
  for (int i = 0; i < Tiles * Tiles; i++)
  {
    if ((TileState)m_state[i] == TileState::NotInWdt || m_minimapIds[i] == 0)
      continue;
    m_minimapsWanted++;
    const CASCFolder::RootEntry e = folder.storageRootEntry((int)m_minimapIds[i]);
    if (!e.listed || !e.local)
    {
      m_thumbState[i] = MinimapMissing;
      m_thumbReason[i] = QString("its minimap (FileDataID %1) is %2").arg(m_minimapIds[i])
                           .arg(!e.listed ? "not in this client's files" : "not downloaded on this computer");
      m_minimapsMissing++;
      continue;
    }
    m_thumbState[i] = MinimapQueued;
    const double dx = (i % Tiles) - cx, dy = (i / Tiles) - cy;
    order.push_back(std::make_pair(dx * dx + dy * dy, i));
  }
  std::sort(order.begin(), order.end());
  for (const auto & o : order)
    m_minimapQueue.push_back(o.second);
  m_minimapClock.start();
  LOG_INFO << "[maps] map" << m_mapId << ":" << m_minimapsWanted << "minimap picture(s) named," << m_minimapQueue.size()
           << "to read," << m_minimapsMissing << "not in the client's files or not downloaded";
  pumpMinimaps();
}

void MapsViewer::pumpMinimaps()
{
  if (!m_active || m_mapPhase != MapPhase::Ready || m_minimapQueue.empty() || m_tilePhase == TilePhase::Loading)
    return;   // (a tile being loaded reads first: the batch goes on when its load ends)
  UnityIpcServer * s = ipc();
  if (!s || !s->isListening() || !clientCurrent(m_epoch, m_generation) || !m_viewer->EnsureMapAssetLease(m_epoch, m_generation))
    return;
  while (!m_minimapQueue.empty() && (int)m_minimapTickets.size() < MinimapBatch)
  {
    const int i = m_minimapQueue.front();
    m_minimapQueue.pop_front();
    MapAssetWorker::Job job;
    job.fileDataID = (int)m_minimapIds[i];
    job.maxBytes = MaxMinimapBytes;
    job.epoch = m_epoch;
    job.generation = m_generation;
    job.role = "minimap";
    job.thumbnailSide = ThumbSide;
    if (!s->submitViewerRead(job))
    {
      m_minimapQueue.push_front(i);
      return;
    }
    m_thumbState[i] = MinimapReading;
    m_minimapTickets[job.ticket] = i;
  }
}

void MapsViewer::stopMinimaps()
{
  m_minimapTimer.Stop();
  if (!m_minimapTickets.empty())
  {
    // Their answers come back as reads of an earlier pick, and are dropped there.
    if (UnityIpcServer * s = ipc())
      s->cancelViewerReads();
    m_minimapTickets.clear();
  }
  m_minimapQueue.clear();
}

void MapsViewer::onMinimapRead(int i, MapAssetWorker::Result & r)
{
  if (i < 0 || i >= (int)m_thumbState.size() || !clientCurrent(m_epoch, m_generation))
    return;
  if (r.outcome == MapAssetWorker::Result::Outcome::Read && r.thumbnail && r.bytes.size() == ThumbSide * ThumbSide * 4)
  {
    m_thumbs[i] = r.bytes;
    m_thumbState[i] = MinimapLoaded;
    m_minimapsLoaded++;
    m_imagesSerial++;
  }
  else
  {
    m_thumbState[i] = MinimapMissing;
    m_minimapsMissing++;
    if (r.outcome != MapAssetWorker::Result::Outcome::Read)
      m_thumbReason[i] = "its minimap was not read (" + MapAssetWorker::outcomeName(r.outcome) + "): " + r.detail;
    else if (r.inspection.state != wow::WdtAvailability::Available)
      m_thumbReason[i] = "its minimap is " + wow::WorldMapCatalog::availabilityName(r.inspection.state) + ": " + r.inspection.detail;
    else
      m_thumbReason[i] = "its minimap could not be decoded: " + r.thumbnailProblem;
    LOG_INFO << "[maps] tile" << tileName(i % Tiles, i / Tiles) << "of map" << m_mapId << ": no picture --" << m_thumbReason[i];
  }
  if (!m_minimapQueue.empty() && !m_minimapTimer.IsRunning())
    m_minimapTimer.StartOnce(MinimapPumpMs);
  if (onImagesChanged)
    onImagesChanged();
  if (!minimapsLoading())
  {
    m_minimapsMs = msOf(m_minimapClock);
    LOG_INFO << "[maps] map" << m_mapId << ": minimap pictures --" << m_minimapsLoaded << "of" << m_minimapsWanted << "shown,"
             << m_minimapsMissing << "without one; the last came" << m_minimapsMs << "ms after the grid";
    changed();
  }
}

quint32 MapsViewer::minimapId(int x, int y) const
{
  return x >= 0 && x < Tiles && y >= 0 && y < Tiles && m_minimapIds.size() == (size_t)(Tiles * Tiles) ? m_minimapIds[y * Tiles + x] : 0;
}

const QByteArray & MapsViewer::thumbnail(int x, int y) const
{
  if (x < 0 || x >= Tiles || y < 0 || y >= Tiles || m_thumbs.empty())
    return NoThumbnail;
  return m_thumbs[y * Tiles + x];
}

QString MapsViewer::minimapState(int x, int y) const
{
  if (x < 0 || x >= Tiles || y < 0 || y >= Tiles || m_thumbState.empty())
    return QString();
  const int i = y * Tiles + x;
  switch ((MinimapState)m_thumbState[i])
  {
    case MinimapNone: return m_minimapIds.empty() ? QString() : QString("no minimap named");
    case MinimapQueued:
    case MinimapReading: return "its minimap is being read";
    case MinimapLoaded: return QString("minimap %1").arg(m_minimapIds[i]);
    case MinimapMissing: return m_thumbReason[i];
  }
  return QString();
}

// ---------------------------------------------------------------- a tile

MapsViewer::TileState MapsViewer::tileState(int x, int y) const
{
  if (x < 0 || x >= Tiles || y < 0 || y >= Tiles)
    return TileState::NotInWdt;
  return (TileState)m_state[y * Tiles + x];
}

quint32 MapsViewer::tileRoot(int x, int y) const
{
  return x >= 0 && x < Tiles && y >= 0 && y < Tiles && !m_roots.empty() ? m_roots[y * Tiles + x] : 0;
}

quint32 MapsViewer::tileTex0(int x, int y) const
{
  return x >= 0 && x < Tiles && y >= 0 && y < Tiles && !m_tex0s.empty() ? m_tex0s[y * Tiles + x] : 0;
}

int MapsViewer::tilesCount(TileState s) const
{
  int n = 0;
  for (unsigned char v : m_state)
    n += (TileState)v == s ? 1 : 0;
  return n;
}

bool MapsViewer::tileExtent(int & minX, int & minY, int & maxX, int & maxY) const
{
  minX = minY = Tiles;
  maxX = maxY = -1;
  for (int y = 0; y < Tiles; y++)
    for (int x = 0; x < Tiles; x++)
      if ((TileState)m_state[y * Tiles + x] != TileState::NotInWdt)
      {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
      }
  return maxX >= 0;
}

QString MapsViewer::stateName(TileState s)
{
  switch (s)
  {
    case TileState::NotInWdt:     return "not part of the map";
    case TileState::NoRoot:       return "no root ADT named";
    case TileState::NotInStorage: return "not in this client's files";
    case TileState::NotLocal:     return "not downloaded";
    case TileState::Available:    return "available";
  }
  return QString();
}

QString MapsViewer::tileReason(int x, int y) const
{
  const QString t = "Tile " + tileName(x, y);
  switch (tileState(x, y))
  {
    case TileState::NotInWdt:
      return t + " is not part of this map: its WDT does not flag it (MAIN).";
    case TileState::NoRoot:
      return t + " is flagged by the WDT, but its MAID names no root ADT for it.";
    case TileState::NotInStorage:
      return t + QString(": its root ADT (FileDataID %1) is not in this client's storage.").arg(tileRoot(x, y));
    case TileState::NotLocal:
      return t + QString(": its root ADT (FileDataID %1) is listed but not downloaded on this computer.").arg(tileRoot(x, y));
    case TileState::Available:
    {
      // The texture ADT is the material's: one the client lacks leaves the tile drawn untextured, not unloadable.
      QString tex0 = "none named (the tile is drawn untextured)";
      if (const quint32 id = tileTex0(x, y))
      {
        const CASCFolder::RootEntry e = static_cast<wow::WoWFolder &>(GAMEDIRECTORY).storageRootEntry((int)id);
        tex0 = QString::number(id) + (!e.listed ? " (not in this client's storage: the tile is drawn untextured)"
                                      : !e.local ? " (not downloaded: the tile is drawn untextured)" : "");
      }
      return t + QString(": root ADT %1, texture ADT %2.").arg(tileRoot(x, y)).arg(tex0);
    }
  }
  return t;
}

// ---------------------------------------------------------------- the target, Enter world, the camera

void MapsViewer::SelectTile(int x, int y)
{
  SetTargetAt(x, y, 0.5, 0.5);
}

void MapsViewer::SetTargetAt(int tx, int ty, double fx, double fy)
{
  if (!hasGrid() || tx < 0 || tx >= Tiles || ty < 0 || ty >= Tiles)
    return;
  double x, y;
  pointIn(tx, ty, std::min(std::max(fx, 0.0), 1.0 - 1e-9), std::min(std::max(fy, 0.0), 1.0 - 1e-9), x, y);
  SetTarget(x, y, false, 0.0);
}

bool MapsViewer::SetTarget(double x, double y, bool hasZ, double z)
{
  int tx, ty;
  if (!hasGrid() || !std::isfinite(x) || !std::isfinite(y) || (hasZ && !std::isfinite(z)) || !tileOf(x, y, tx, ty))
  {
    m_tileDetail = hasGrid() ? QString("X %1, Y %2 is not on this map's 64 x 64 tiles (each coordinate within +-%3).")
                                 .arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(32.0 * TileSize, 0, 'f', 2)
                             : QString("Pick a map first.");
    changed();
    return false;
  }
  m_hasTarget = true;
  m_targetX = x;
  m_targetY = y;
  m_targetHasZ = hasZ;
  m_targetZ = hasZ ? z : 0.0;
  // The target's tile is the tile picked: the grid's ring, and what Enter world loads. A load in flight goes on, and what
  // is on screen stays, until Enter world.
  m_tileX = tx;
  m_tileY = ty;
  if (m_tilePhase != TilePhase::Loading && m_tilePhase != TilePhase::Loaded)
  {
    m_tilePhase = tileState(tx, ty) == TileState::Available ? TilePhase::None : TilePhase::Unavailable;
    m_tileDetail = m_tilePhase == TilePhase::Unavailable ? tileReason(tx, ty) : QString();
  }
  else if (tileState(tx, ty) != TileState::Available)
    m_tileDetail = tileReason(tx, ty);
  LOG_INFO << "[maps] target" << targetLine();
  changed();
  return true;
}

QString MapsViewer::targetLine() const
{
  if (!m_hasTarget)
    return QString();
  int tx, ty;
  tileOf(m_targetX, m_targetY, tx, ty);
  return QString("Tile %1, %2  X %3  Y %4  Z %5").arg(tx).arg(ty).arg(m_targetX, 0, 'f', 1).arg(m_targetY, 0, 'f', 1)
    .arg(m_targetHasZ ? QString::number(m_targetZ, 'f', 1) : QString("on the ground"));
}

bool MapsViewer::canEnter(QString & why) const
{
  why.clear();
  UnityIpcServer * s = ipc();
  int tx, ty;
  if (!hasGrid())
    why = m_mapId < 0 ? QString("Pick a map first.") : m_mapDetail.isEmpty() ? QString("The map's tiles are not known yet.") : m_mapDetail;
  else if (m_globalWmoRecords > 0 && m_tilesInWdt == 0)
    why = m_mapDetail;
  else if (!m_hasTarget)
    why = "Click the overhead map, or type X and Y, to set where to enter the world.";
  else if (!tileOf(m_targetX, m_targetY, tx, ty) || tileState(tx, ty) != TileState::Available)
    why = tileReason(m_tileX, m_tileY);
  else if (!s || !s->isUnityReady())
    why = "The Unity viewport is not ready.";
  else if (m_viewer->m_clientLoading || UnityAssetAccess::isClientLoading())
    why = "A client is being loaded.";
  return why.isEmpty();
}

bool MapsViewer::targetOnShownTile() const
{
  int tx, ty;
  return m_hasTarget && m_shownX >= 0 && m_shownMap == m_mapId && tileOf(m_targetX, m_targetY, tx, ty) && tx == m_shownX && ty == m_shownY;
}

UnityIpcServer::WorldCamera MapsViewer::flyCameraOnShownTile()
{
  // At the target when it is on the tile on screen; else at that tile's middle, on the ground (a target typed for another
  // tile waits for Enter world, and the free camera never leaves the tile it stands on).
  if (targetOnShownTile())
    return flyCameraAtTarget();
  UnityIpcServer::WorldCamera c = flyCameraAtTarget();
  pointIn(m_shownX, m_shownY, 0.5, 0.5, c.x, c.y);
  c.hasZ = false;
  c.z = 0.0;
  return c;
}

UnityIpcServer::WorldCamera MapsViewer::flyCameraAtTarget()
{
  UnityIpcServer::WorldCamera c;
  c.request = ++m_cameraSerial;
  c.mode = "fly";
  c.x = m_targetX;
  c.y = m_targetY;
  c.z = m_targetZ;
  c.hasZ = m_targetHasZ;
  c.yaw = m_lookYaw;
  c.pitch = m_lookPitch;
  c.eyeHeight = EyeHeight;
  return c;
}

void MapsViewer::EnterWorld()
{
  QString why;
  if (!canEnter(why))
  {
    m_tileDetail = why;
    LOG_INFO << "[maps] Enter world refused:" << why;
    changed();
    return;
  }
  UnityIpcServer * s = ipc();
  const bool flies = s->playerFliesWorld();
  int tx, ty;
  tileOf(m_targetX, m_targetY, tx, ty);
  UnityIpcServer::WorldCamera camera = flies ? flyCameraAtTarget() : UnityIpcServer::WorldCamera();
  m_cameraMode = CameraMode::Fly;
  // The target's tile on screen already: only the camera moves.
  if (m_tilePhase == TilePhase::Loaded && m_shownX == tx && m_shownY == ty && m_shownMap == m_mapId)
  {
    if (!flies)
    {
      m_worldShown = true;
      m_cameraMode = CameraMode::Overview;
      m_cameraDetail = "The Unity renderer in use has no free camera (protocol 15 is needed): the tile is shown from above.";
      changed();
      return;
    }
    s->sendSetWorldCamera(camera);
    m_cameraRequest = camera.request;
    m_entering = true;
    m_cameraDetail = "Placing the camera at the target.";
    changed();
    return;
  }
  QElapsedTimer clock;
  clock.start();
  const int load = m_viewer->RequestTerrainTile(m_mapId, tx, ty, why, true, flies ? &camera : nullptr);
  m_timings.requestMs = msOf(clock);
  if (load <= 0)
  {
    m_tilePhase = TilePhase::Failed;
    m_tileDetail = "The world could not be entered: " + why;
    pumpMinimaps();   // (a load before it may have held the pictures back)
    changed();
    return;
  }
  m_tileX = tx;
  m_tileY = ty;
  m_load = load;
  m_tilePhase = TilePhase::Loading;
  m_tileDetail = "Entering the world at tile " + tileName(tx, ty) + ": its terrain and textures are being read and built.";
  m_entering = true;
  m_cameraRequest = flies ? camera.request : 0;
  m_cameraDetail = flies ? QString("The free camera goes to the target when the tile is shown.")
                         : QString("The Unity renderer in use has no free camera (protocol 15 is needed): the tile will be shown "
                                   "from above.");
  m_loadClock.start();
  LOG_INFO << "[maps] Enter world at" << targetLine() << ": tile" << tileName(tx, ty) << "requested, load" << load << "("
           << m_timings.requestMs << "ms on this thread, no WDT read)" << (flies ? "with the free camera" : "without a camera");
  changed();
}

void MapsViewer::BackToMap()
{
  if (!m_worldShown && !m_entering)
    return;
  m_worldShown = false;
  // Asked while the world was being entered: the load goes on (the world will be behind the map), but its camera's
  // answer no longer takes the viewport.
  m_entering = false;
  m_cameraRequest = 0;
  LOG_INFO << "[maps] back to the map; the world stays loaded behind it";
  changed();
}

void MapsViewer::leaveWorld()
{
  m_worldShown = false;
  m_entering = false;
  m_cameraRequest = 0;
  m_cameraPlaced = false;
  m_cameraHeld = false;
  m_cameraDetail.clear();
}

void MapsViewer::SetCameraMode(CameraMode mode)
{
  UnityIpcServer * s = ipc();
  if (!m_worldShown || m_shownX < 0 || !s || !s->playerFliesWorld())
  {
    m_cameraMode = mode;
    changed();
    return;
  }
  UnityIpcServer::WorldCamera c;
  if (mode == CameraMode::Fly)
    c = flyCameraOnShownTile();
  else
  {
    c.request = ++m_cameraSerial;
    c.mode = "overview";
  }
  if (s->sendSetWorldCamera(c))
    m_cameraRequest = c.request;
  m_cameraMode = mode;
  changed();
}

void MapsViewer::Look(double yaw, double pitch)
{
  m_lookYaw = yaw;
  m_lookPitch = pitch;
  UnityIpcServer * s = ipc();
  if (!m_worldShown || m_shownX < 0 || !s || !s->playerFliesWorld())
    return;
  const UnityIpcServer::WorldCamera c = flyCameraOnShownTile();
  if (s->sendSetWorldCamera(c))
    m_cameraRequest = c.request;
  m_cameraMode = CameraMode::Fly;
}

void MapsViewer::OnWorldCameraApplied(const UnityIpcServer::WorldCameraApplied & a)
{
  if (a.request == 0 || a.request != m_cameraRequest)
  {
    LOG_INFO << "[maps] the answer to camera request" << a.request << "came after request" << m_cameraRequest << ": not shown";
    return;
  }
  // The camera Enter world sent with the load is placed when the player shows the tile, a moment before the load is
  // reported built: its answer is held until that report, so the world takes the viewport with its tile known (never
  // with the "Loading tile" notice over the player's first frame), and not at all when the load ends otherwise.
  if (a.load != 0 && a.load == m_load && m_tilePhase == TilePhase::Loading)
  {
    m_heldCamera = a;
    m_cameraHeld = true;
    LOG_INFO << "[maps] camera" << a.request << a.status << "with load" << a.load << "on screen: held for the load's report";
    return;
  }
  applyCamera(a);
  changed();
}

void MapsViewer::applyCamera(const UnityIpcServer::WorldCameraApplied & a)
{
  m_cameraRequest = 0;
  m_entering = false;
  if (a.status == "applied")
  {
    m_worldShown = true;
    m_cameraPlaced = a.mode == "fly";
    m_cameraX = a.x;
    m_cameraY = a.y;
    m_cameraZ = a.z;
    m_cameraMode = a.mode == "overview" ? CameraMode::Overview : CameraMode::Fly;
    m_cameraDetail = a.mode == "overview"
                       ? QString("Tile overview.")
                       : QString("Free camera at X %1  Y %2  Z %3%4. Drag to look, the wheel to fly forwards and back; W A S D, "
                                 "Q and E once a click in the viewport has given it the keys.")
                           .arg(a.x, 0, 'f', 1).arg(a.y, 0, 'f', 1).arg(a.z, 0, 'f', 1)
                           .arg(a.ground ? QString(" (on the ground, %1, plus %2)").arg(a.groundZ, 0, 'f', 1).arg(EyeHeight, 0, 'f', 1)
                                         : a.hasGroundZ ? QString() : QString(" (%1)").arg(a.reason));
  }
  else
  {
    // A tile on screen whose camera could not be placed is still shown, framed from above.
    if (a.load != 0 && a.load == m_load)
      m_worldShown = true;
    m_cameraDetail = "The camera could not be placed: " + a.reason;
  }
  LOG_INFO << "[maps] camera" << a.request << a.status << ":" << m_cameraDetail;
}

void MapsViewer::OnTerrainTileLoaded(const UnityIpcServer::TerrainTileReport & r)
{
  if (m_load == 0 || r.load != m_load)
  {
    // A load this viewer no longer waits for: replaced by a newer click (answered "superseded"), cleared, or of a client
    // replaced since. Never shown.
    if (r.load > 0)
      LOG_INFO << "[maps] load" << r.load << "(tile" << tileName(r.tileX, r.tileY) << "of map" << r.mapId << ") answered" << r.status
               << "after this viewer moved on (now waiting for load" << m_load << "): not shown";
    return;
  }
  m_timings.loadMs = msOf(m_loadClock);
  m_timings.playerTotalMs = r.totalMs;
  const QString t = tileName(r.tileX, r.tileY);
  if (r.status == "built")
  {
    m_tilePhase = TilePhase::Loaded;
    m_shownX = r.tileX;
    m_shownY = r.tileY;
    m_shownMap = r.mapId;
    m_shownView = r.view;
    QString material;
    const int textured = r.chunkState("textured");
    if (!r.hasMaterial)
      material = "Geometry only: the renderer in use offers no terrain textures.";
    else if (r.materialState == "textured")
      material = QString("Textured: every drawable chunk (%1) drawn with its textures.").arg(textured);
    else if (r.materialState == "partial")
      material = QString("Partly textured: %1 of %2 chunks drawn with their textures; the others in neutral shading "
                         "(their material is not supported or not complete).").arg(textured).arg(r.chunksBuilt);
    else if (r.materialState == "unsupported")
      material = "Not textured: this tile's material is not supported (" + r.materialReason +
                 "); every chunk is drawn in neutral diagnostic shading.";
    else
      material = "Not textured: " + (r.materialReason.isEmpty() ? QString("the material is unavailable") : r.materialReason) +
                 "; every chunk is drawn in neutral diagnostic shading.";
    QString geometry;
    if (r.chunksUnsupported > 0)
      geometry += QString(" %1 chunk(s) of geometry are not supported and not drawn.").arg(r.chunksUnsupported);
    if (r.holeCells > 0)
      geometry += QString(" %1 hole cell(s).").arg(r.holeCells);
    m_tileDetail = "Tile " + t + " loaded in " + QString::number(m_timings.loadMs / 1000.0, 'f', 2) + " s. " + material + geometry;
  }
  else if (r.status == "failed")
  {
    m_tilePhase = TilePhase::Failed;
    m_tileDetail = "Tile " + t + " could not be loaded: " + r.reason +
                   (r.hostState.isEmpty() ? QString() : " (the host answered " + r.hostState + ": " + r.hostDetail + ")");
  }
  else if (r.status == "superseded")
  {
    m_tilePhase = TilePhase::Superseded;
    m_tileDetail = "The load of tile " + t + " was replaced before it was built: " + r.reason;
  }
  else
  {
    m_tilePhase = TilePhase::Cleared;
    m_tileDetail = "The load of tile " + t + " was cleared: " + r.reason;
  }
  if (r.status != "built")
  {
    m_load = 0;
    // Enter world ended without a world: the overhead map stays, with why.
    m_entering = false;
    m_cameraRequest = 0;
  }
  else if (m_entering && m_cameraRequest == 0)
  {
    // A player without the free camera: the tile is shown framed from above.
    m_entering = false;
    m_worldShown = true;
    m_cameraMode = CameraMode::Overview;
  }
  if (m_cameraHeld)
  {
    // The camera answer held for this report: taken now the tile is built, dropped when the load ended otherwise (or a
    // later camera request replaced it).
    m_cameraHeld = false;
    if (r.status == "built" && m_heldCamera.load == r.load && m_heldCamera.request == m_cameraRequest)
      applyCamera(m_heldCamera);
  }
  LOG_INFO << "[maps] load" << r.load << "tile" << t << "of map" << r.mapId << ":" << r.status << "after" << m_timings.loadMs
           << "ms (the player's own total" << r.totalMs << "ms; view" << r.view << ") --" << m_tileDetail;
  pumpMinimaps();   // the pictures waited for the load
  changed();
}

void MapsViewer::ClearTerrain()
{
  if (m_tilePhase != TilePhase::Loading && m_tilePhase != TilePhase::Loaded && m_shownX < 0)
    return;
  m_viewer->ClearTerrain("the Maps viewer's Clear terrain");
  m_tilePhase = TilePhase::Cleared;
  m_tileDetail = "The world was cleared. Enter world to load it again.";
  m_load = 0;
  m_shownX = m_shownY = m_shownMap = -1;
  m_shownView.clear();
  leaveWorld();
  LOG_INFO << "[maps] terrain cleared";
  pumpMinimaps();
  changed();
}

void MapsViewer::SetDisplayMode(int index)
{
  if (index < 0 || index >= (int)displayModes().size())
    return;
  m_displayMode = index;
  const QString view = displayModes()[index].view;
  UnityIpcServer * s = ipc();
  if (!s || !s->playerInspectsMapTiles())
  {
    m_viewDetail = "The Unity renderer in use cannot switch the terrain view (protocol 14 is needed).";
    changed();
    return;
  }
  s->sendSetTerrainView(view);
  m_viewSent = view;
  m_viewClock.start();
  m_viewDetail = QString("Drawing as ") + displayModes()[index].label + ".";
  changed();
}

void MapsViewer::OnTerrainViewApplied(const UnityIpcServer::TerrainViewApplied & a)
{
  // Only the answer to the view asked for last counts (two picks in a row: the first answer is not shown).
  if (a.view != m_viewSent)
  {
    LOG_INFO << "[maps] the answer to view" << a.view << "came after view" << m_viewSent << "was asked for: not shown";
    return;
  }
  m_timings.viewMs = msOf(m_viewClock);
  m_timings.rebuildMs = a.rebuildMs;
  QString label = a.view;
  for (const DisplayMode & m : displayModes())
    if (a.view == m.view)
      label = m.label;
  if (a.status == "applied" && m_shownX < 0)
    m_viewDetail = QString("%1: applies to the next tile loaded.").arg(label);   // the tile went since (cleared, left)
  else if (a.status == "applied")
  {
    m_shownView = a.reason.startsWith("drawn ") ? a.reason.mid(6) : a.view;
    m_viewDetail = QString("%1: the tile drawn again in %2 ms (%3 chunk(s) textured, %4 neutral), nothing read again.")
                     .arg(label).arg(a.rebuildMs, 0, 'f', 1).arg(a.texturedChunks).arg(a.neutralChunks);
  }
  else if (a.status == "noTile")
    m_viewDetail = QString("%1: applies to the next tile loaded.").arg(label);
  else
    m_viewDetail = QString("%1 could not be drawn: %2").arg(label, a.reason);
  LOG_INFO << "[maps] view" << a.view << a.status << "in" << m_timings.viewMs << "ms (player rebuild" << a.rebuildMs << "ms)";
  changed();
}

// ---------------------------------------------------------------- what is shown

QString MapsViewer::statusLine() const
{
  if (m_mapPhase == MapPhase::None)
    return m_mapDetail.isEmpty() ? QString("No map selected.") : m_mapDetail;
  if (m_mapPhase != MapPhase::Ready)
    return m_mapDetail;
  QString s = QString("%1 of %2 tiles available.").arg(m_tilesAvailable).arg(m_tilesInWdt);
  if (m_minimapsWanted > 0)
    s += minimapsLoading() ? QString(" Minimap pictures: %1 of %2.").arg(m_minimapsLoaded).arg(m_minimapsWanted)
                           : QString(" Minimap pictures: %1%2.").arg(m_minimapsLoaded)
                               .arg(m_minimapsMissing > 0 ? QString(" (%1 without one)").arg(m_minimapsMissing) : QString());
  if (!m_mapDetail.isEmpty())
    s += " " + m_mapDetail;
  return s;
}

bool MapsViewer::viewportNotice(QString & title, QString & detail) const
{
  title.clear();
  detail.clear();
  if (!UnityAssetAccess::hasActiveClient())
  {
    title = "No map selected";
    detail = "Load a World of Warcraft client to browse its maps.";
    return true;
  }
  const std::shared_ptr<wow::WorldMapCatalog> catalog = m_viewer->mapCatalog();
  if (!catalog || catalog->status() != wow::WorldMapCatalog::Status::Ready)
  {
    title = "Maps cannot be listed";
    detail = catalog ? "This client's maps cannot be listed: " + wow::WorldMapCatalog::statusName(catalog->status()) +
                         (catalog->statusDetail().isEmpty() ? QString() : " (" + catalog->statusDetail() + ")")
                     : QString("There is no map catalogue.");
    return true;
  }
  const QString map = m_mapId >= 0 ? (m_mapName.isEmpty() ? m_mapDirectory : m_mapName) + QString(" [%1]").arg(m_mapId) : QString();
  switch (m_mapPhase)
  {
    case MapPhase::None:
      title = "No map selected";
      detail = "Select a map in Browse, or search for one by name or MapID.";
      return true;
    case MapPhase::NoWdt:
    case MapPhase::Failed:
      title = map.isEmpty() ? QString("Map cannot be shown") : map + " cannot be shown";
      detail = m_mapDetail;
      return true;
    case MapPhase::WaitingPlayer:
    case MapPhase::Reading:
    case MapPhase::Inspecting:
      title = "Reading " + map;
      detail = m_mapDetail;
      return true;
    case MapPhase::Ready:
      break;
  }
  const QString t = m_tileX >= 0 ? tileName(m_tileX, m_tileY) : QString();
  switch (m_tilePhase)
  {
    case TilePhase::None:
    case TilePhase::Cleared:
      title = m_tilesAvailable > 0 ? QString("No world entered") : map + " has no tile to show";
      detail = m_tilesAvailable > 0 ? (m_tilePhase == TilePhase::Cleared ? m_tileDetail
                                                                         : QString("Click the overhead map to set where to enter "
                                                                                   "the world, then Enter world."))
                                    : m_mapDetail;
      return true;
    case TilePhase::Unavailable:
      title = "Tile " + t + " cannot be loaded";
      detail = m_tileDetail;
      return true;
    case TilePhase::Loading:
      title = "Loading tile " + t;
      detail = map + ": the tile's terrain and textures are being read and built.";
      return true;
    case TilePhase::Failed:
      title = "Tile " + t + " could not be loaded";
      detail = m_tileDetail;
      return true;
    case TilePhase::Superseded:
      title = "Tile " + t + " was replaced";
      detail = m_tileDetail;
      return true;
    case TilePhase::Loaded:
      return false;
  }
  return false;
}
