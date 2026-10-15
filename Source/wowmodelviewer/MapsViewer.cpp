/*
 * MapsViewer.cpp -- the Maps viewer's state (MapsViewer.h; docs/world-viewer/05-maps-viewer-ui.md).
 */

#include "MapsViewer.h"

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
    m_tileDetail = "The terrain was taken away when the Maps viewer was left. Click a tile to load it again.";
  }
  m_load = 0;
  m_shownX = m_shownY = m_shownMap = -1;
  m_shownView.clear();
  LOG_INFO << "[maps] left; the terrain cleared";
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
  changed();
}

void MapsViewer::resetMap()
{
  if (m_readTicket != 0)
    if (UnityIpcServer * s = ipc())
      s->cancelViewerReads();
  m_readTicket = 0;
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

void MapsViewer::OnWdtRead(MapAssetWorker::Result & result)
{
  if (result.ticket == 0 || result.ticket != m_readTicket)
  {
    LOG_INFO << "[maps] a WDT read (ticket" << (qulonglong)result.ticket << ") of an earlier pick came back: dropped";
    // The pick that waited for it is read now.
    UnityIpcServer * s = ipc();
    if (m_readDeferred && m_mapId >= 0 && s && s->viewerReadsInFlight() == 0)
    {
      beginRead();
      changed();
    }
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
    m_tileDetail = "The Unity viewport was restarted. Click a tile to load it again.";
  }
  m_load = 0;
  m_shownX = m_shownY = m_shownMap = -1;
  if ((m_mapPhase == MapPhase::WaitingPlayer || m_mapPhase == MapPhase::Inspecting) && !m_wdtBytes.isEmpty())
    sendInspection();
  else if (m_mapPhase == MapPhase::WaitingPlayer && m_mapId >= 0 && m_readTicket == 0)
    beginRead();   // the WDT waited for a server to take its read back
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
  if (r.kind == "GlobalWmo")
    m_mapDetail = "This map has no terrain tiles: it is one world model (open it in Buildings).";
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

void MapsViewer::SelectTile(int x, int y)
{
  if (!hasGrid() || x < 0 || x >= Tiles || y < 0 || y >= Tiles)
    return;
  if (m_tilePhase == TilePhase::Loaded && m_shownX == x && m_shownY == y && m_shownMap == m_mapId)
    return;   // on screen already
  if (m_tilePhase == TilePhase::Loading && m_tileX == x && m_tileY == y)
    return;   // being loaded already (a key held down, a double click): not asked for again
  m_tileX = x;
  m_tileY = y;
  if (tileState(x, y) != TileState::Available)
  {
    // Picked, not loaded: what is on screen stays, and a load in flight goes on (its report is still taken: m_load).
    m_tilePhase = TilePhase::Unavailable;
    m_tileDetail = tileReason(x, y);
    LOG_INFO << "[maps] tile" << tileName(x, y) << "of map" << m_mapId << "picked, not loaded:" << m_tileDetail;
    changed();
    return;
  }
  QElapsedTimer clock;
  clock.start();
  QString why;
  const int load = m_viewer->RequestTerrainTile(m_mapId, x, y, why, true);
  m_timings.requestMs = msOf(clock);
  if (load <= 0)
  {
    m_tilePhase = TilePhase::Failed;
    m_tileDetail = "The load was not sent: " + why;
    changed();
    return;
  }
  m_load = load;
  m_tilePhase = TilePhase::Loading;
  m_tileDetail = "Loading " + tileName(x, y) + ".";
  m_loadClock.start();
  LOG_INFO << "[maps] tile" << tileName(x, y) << "of map" << m_mapId << "requested: load" << load << "(" << m_timings.requestMs
           << "ms on this thread, no WDT read)";
  changed();
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
    m_load = 0;
  LOG_INFO << "[maps] load" << r.load << "tile" << t << "of map" << r.mapId << ":" << r.status << "after" << m_timings.loadMs
           << "ms (the player's own total" << r.totalMs << "ms; view" << r.view << ") --" << m_tileDetail;
  changed();
}

void MapsViewer::ClearTerrain()
{
  if (m_tilePhase != TilePhase::Loading && m_tilePhase != TilePhase::Loaded && m_shownX < 0)
    return;
  m_viewer->ClearTerrain("the Maps viewer's Clear terrain");
  m_tilePhase = TilePhase::Cleared;
  m_tileDetail = "The terrain was cleared. Click a tile to load it again.";
  m_load = 0;
  m_shownX = m_shownY = m_shownMap = -1;
  m_shownView.clear();
  LOG_INFO << "[maps] terrain cleared";
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
      title = m_tilesAvailable > 0 ? QString("No tile selected") : map + " has no tile to show";
      detail = m_tilesAvailable > 0 ? (m_tilePhase == TilePhase::Cleared ? m_tileDetail
                                                                         : QString("Click an available tile in the tile grid "
                                                                                   "under the map list in Browse."))
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
