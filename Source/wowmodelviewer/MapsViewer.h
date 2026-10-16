/*
 * MapsViewer.h
 *
 * THE MAPS VIEWER (docs/world-viewer/05-maps-viewer-ui.md): the fourth viewer mode's state, between the user's choices
 * (a map in Browse, a target on the overhead map, Enter world, a camera, a display mode) and the verified world pipeline of
 * stages 02-04B. It owns no widget: Browse lists the catalogue's maps (FileControl), the overhead map in the viewport's
 * place draws this state (MapOverview), the panel under Browse shows its facts and controls (MapTilePanel), and once a
 * world is entered the viewport shows it (the Unity player).
 *
 *   a map picked     its WDT read whole by the MAP ASSET WORKER (off the GUI thread; the read, its frame checks and its
 *                    content key are the catalogue's own, WorldMapCatalog::inspectInStorage), then handed to the player
 *                    whose verified parser returns the tile table (inspectMapTiles -> mapTilesInspected, protocol 14):
 *                    the host never parses a WDT. Each tile's root ADT (MAID word 0) is then looked up in the client's
 *                    storage root: listed, on this computer (never read). A tile is available only when its MAIN flag is
 *                    set, MAID names a root ADT, and the storage lists that file and has it locally.
 *   the pictures     the tiles' minimap images (MAID word 7, in the table since protocol 15) read and reduced to thumbnails
 *                    on the worker (MapThumbnail), a batch at a time, none while a tile loads, nearest the map's middle
 *                    first; cancelled with the map.
 *   a target         a point of the game's world frame (x north, y west): clicked inside a cell of the overhead map, or
 *                    typed (X, Y, and Z or "on the ground"). A target off the 64 x 64 tiles is refused; one on a tile that
 *                    cannot be loaded is kept, with why the world cannot be entered there.
 *   Enter world      the target's tile requested (ModelViewer::RequestTerrainTile: Stage 03/04 terrain, protocol 13
 *                    serving, the Stage 04B worker), with the WDT inspection the worker made, so the GUI thread reads
 *                    nothing, and with the camera (protocol 15): the player puts its free camera at the target when the tile
 *                    is shown, and answers worldCameraApplied; the world then has the viewport. The target's tile already
 *                    on screen: the camera alone is moved (setWorldCamera). Back to map gives the viewport to the overhead
 *                    map again; the world stays loaded behind it.
 *   a camera         the free camera at the target, or the tile's overview (setWorldCamera), nothing reloaded.
 *   world models     (protocol 16, docs/world-viewer/06-world-models-in-world.md) Enter world loads the tile with the world
 *                    models its obj0 ADT places (ModelViewer::RequestWorld): the world takes the viewport when the player
 *                    first shows it (worldShown), while its world models may still be loading (worldProgress); the load's
 *                    report says how many were placed, failed or unsupported. A WMO-ONLY MAP (its WDT names one global
 *                    world model and no tile) is no longer refused: its world model's root is read by the worker and
 *                    handed to the player (inspectWorldModel), whose parser answers with the placed root box and every
 *                    group's box (worldModelInspected) -- the FOOTPRINT the overhead map draws instead of the tile grid. A
 *                    click inside it sets the target; Enter world loads the world model (no tile) with the free camera at
 *                    the target, standing on the highest floor under it, or -- no target -- at the world model's own start
 *                    point (the player's, from its geometry).
 *   a display mode   setTerrainView (protocol 14): the tile on screen built again in it by the player, nothing read.
 *
 * ISOLATION. Every read and answer carries the client epoch and storage generation it was made under, and a serial of
 * its own (the map's inspection): one that comes back for another client, another map or a superseded inspection is
 * dropped, said in the log, and never shown. A client replaced forgets everything (ClientReplaced); the worker's read
 * is cancelled with the server's (UnityIpcServer::cancelMapReads).
 *
 * GUI thread only.
 */

#ifndef MAPSVIEWER_H
#define MAPSVIEWER_H

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QString>

#include <wx/timer.h>

#include "MapAssetWorker.h"
#include "UnityIpcServer.h"

class ModelViewer;

class MapsViewer
{
public:
  static const int Tiles = 64;

  // A tile as the tile grid shows it, from the WDT's own table and the client's storage root, never assumed.
  enum class TileState : unsigned char
  {
    NotInWdt,      // the WDT's MAIN flag is clear: the map has no terrain there
    NoRoot,        // MAIN flags it, but MAID names no root ADT for it
    NotInStorage,  // MAID names a root ADT the client's storage does not list
    NotLocal,      // listed, but its data is not on this computer (a partly downloaded client)
    Available,     // listed and local: a load can read it
  };

  // What the map picked is at (the order a pick goes through).
  enum class MapPhase
  {
    None,           // no map picked
    NoWdt,          // the catalogue gives it no WDT (detail says why)
    WaitingPlayer,  // its WDT is read, the player is not ready (or too old) to read the tile table
    Reading,        // its WDT is being read by the map asset worker
    Inspecting,     // the player is reading the tile table
    Failed,         // the WDT could not be read or the table could not be had (detail says why)
    Ready,          // the tile grid is known
  };

  // What the tile picked is at.
  enum class TilePhase
  {
    None,         // no tile picked
    Unavailable,  // a tile that cannot be loaded (detail says why)
    Loading,      // requested, the player is building it
    Loaded,       // built and on screen
    Failed,       // the load failed (detail says why, the player's reason or the host's refusal)
    Superseded,   // replaced by a newer request before it was built
    Cleared,      // taken away (Clear terrain, or the viewer left)
  };

  // The display modes offered: the player's own views (no new shading), "auto" being stage 04's default -- textured
  // where the material has drawable chunks, the stage 03 neutral shading otherwise.
  struct DisplayMode
  {
    const char * label;
    const char * view;   // setTerrainView's name
    const char * what;   // what it shows, for the status line
  };
  static const std::vector<DisplayMode> & displayModes();

  // Timings of the last of each step (milliseconds; -1 not measured yet), for the log, the tests and the document.
  struct Timings
  {
    double selectMs = -1.0;        // GUI: the pick's own work (catalogue lookup, the worker job given)
    double wdtWaitMs = -1.0;       // worker: from the job given to its start
    double wdtReadMs = -1.0;       // worker: the read, its frame checks and its content key
    double wdtResultMs = -1.0;     // GUI: taking the result back and sending the bytes to the player
    double inspectRoundTripMs = -1.0;  // from inspectMapTiles sent to mapTilesInspected handled
    double inspectParseMs = -1.0;  // player: its parse
    double gridMs = -1.0;          // GUI: the tile table made (the storage root's answer for every root ADT)
    double pickToGridMs = -1.0;    // from the pick to the grid known
    double requestMs = -1.0;       // GUI: a tile's request (ResolveTerrainTile without a WDT read, the send)
    double loadMs = -1.0;          // from the click to the player's "built" report handled
    double playerTotalMs = -1.0;   // player: the load's own total
    double viewMs = -1.0;          // from setTerrainView sent to terrainViewApplied handled
    double worldShownMs = -1.0;    // from the click to worldShown handled (a world load: the world takes the viewport)
    double footprintMs = -1.0;     // from the grid known to the footprint known (a WMO-only map)
    double rebuildMs = -1.0;       // player: the rebuild in the view
    double uiUpdateMs = -1.0;      // GUI: the last update of what is shown (the grid panel, the viewport's notice, labels)
    double uiUpdateMaxMs = -1.0;   // ... the longest since the viewer was made
  };
  // The viewer's own GUI time per update (ModelViewer's onChanged reports it).
  void noteUiUpdate(double ms)
  {
    m_timings.uiUpdateMs = ms;
    if (ms > m_timings.uiUpdateMaxMs)
      m_timings.uiUpdateMaxMs = ms;
  }

  explicit MapsViewer(ModelViewer * viewer);

  // THE WORLD FRAME (docs/world-viewer/00-audit.md 6.1): yards, x north, y west; tile (tx, ty) -- tx the column, east;
  // ty the row, south -- spans x from (32 - ty)T down to (32 - ty - 1)T and y from (32 - tx)T down to (32 - tx - 1)T,
  // T = 1600/3. The tile under a point, and the point at fractions of a tile (fx east, fy south of its north-west corner).
  static constexpr double TileSize = 1600.0 / 3.0;
  static bool tileOf(double x, double y, int & tx, int & ty);
  static void pointIn(int tx, int ty, double fx, double fy, double & x, double & y);
  // The free camera's eye above the ground when no height is given (yards).
  static constexpr double EyeHeight = 2.0;

  // THE VIEWER MODE: entered (the grid comes back as it was left, for the same client), left (the terrain cleared).
  void Entered();
  void Left();
  bool active() const { return m_active; }

  // A CLIENT: being replaced (everything about the old one forgotten now; its reads cancelled by the server), loaded.
  void ClientReplaced();
  void ClientLoaded();

  // THE USER'S CHOICES.
  void SelectMap(int mapId);
  // THE TARGET. SetTarget: a typed point (false, and said, when it is off the map's tiles). SetTargetAt: a click on the
  // overhead map, at fractions of cell (tx, ty). SelectTile: the cell's middle (the keyboard's pick).
  bool SetTarget(double x, double y, bool hasZ, double z);
  void SetTargetAt(int tx, int ty, double fx, double fy);
  // No target (the fields emptied): a WMO-only map is entered at its start point again; a tile map has no tile picked.
  void ClearTarget();
  void SelectTile(int x, int y);
  bool hasTarget() const { return m_hasTarget; }
  double targetX() const { return m_targetX; }
  double targetY() const { return m_targetY; }
  double targetZ() const { return m_targetZ; }
  bool targetHasZ() const { return m_targetHasZ; }
  QString targetLine() const;
  // ENTER WORLD and BACK TO MAP (see the header). canEnter: false, and why, when the world cannot be entered at the target.
  bool canEnter(QString & why) const;
  void EnterWorld();
  void BackToMap();
  // The viewport shows the overhead map (not the world): in this viewer, until a world is entered, and after Back to map.
  bool showsOverview() const { return m_active && !m_worldShown; }
  bool worldShown() const { return m_worldShown; }
  bool entering() const { return m_entering; }
  // THE CAMERA in the world: the free camera at the target, or the tile's overview. Look: the free camera put at the target
  // again, facing yaw (degrees clockwise from north) and pitch (degrees, up positive).
  enum class CameraMode { Fly, Overview };
  void SetCameraMode(CameraMode mode);
  CameraMode cameraMode() const { return m_cameraMode; }
  void Look(double yaw, double pitch);
  QString cameraDetail() const { return m_cameraDetail; }
  // The camera's last placement the player answered "applied" (game frame), and whether one is.
  bool cameraPlaced() const { return m_cameraPlaced; }
  double cameraX() const { return m_cameraX; }
  double cameraY() const { return m_cameraY; }
  double cameraZ() const { return m_cameraZ; }
  void ClearTerrain();
  void SetDisplayMode(int index);
  int displayMode() const { return m_displayMode; }

  // THE PLAYER'S AND THE WORKER'S ANSWERS (wired by ModelViewer to its server).
  void OnPlayerReady();
  void OnWdtRead(MapAssetWorker::Result & result);
  void OnTilesInspected(const UnityIpcServer::MapTilesInspection & inspection);
  void OnTerrainTileLoaded(const UnityIpcServer::TerrainTileReport & report);
  void OnTerrainViewApplied(const UnityIpcServer::TerrainViewApplied & applied);
  void OnWorldCameraApplied(const UnityIpcServer::WorldCameraApplied & applied);
  // Protocol 16: a world load's scene on screen, its progress, and a WMO-only map's footprint.
  void OnWorldShown(const UnityIpcServer::WorldShown & shown);
  void OnWorldProgress(const UnityIpcServer::WorldProgress & progress);
  void OnWorldModelInspected(const UnityIpcServer::WorldModelInspection & inspection);

  // THE OVERHEAD MAP'S PICTURES (see the header): ThumbSide x ThumbSide RGBA thumbnails, MinimapBatch read at a time.
  static const int ThumbSide = 32;
  static const int MinimapBatch = 8;
  // The next batch is given from a timer this long after a picture came, never from the picture's own handler: a result
  // posts the server a wake, and wx runs the events that become pending while it runs them, so reads given from the handler
  // would chain wake after wake and keep the window from painting or taking input until the last picture.
  static const int MinimapPumpMs = 10;
  // A tile's thumbnail (empty: none, or not yet), and its picture's state in words (the hover line).
  const QByteArray & thumbnail(int x, int y) const;
  QString minimapState(int x, int y) const;
  // A tile's minimap FileDataID (MAID word 7), 0 for none.
  quint32 minimapId(int x, int y) const;
  int minimapsWanted() const { return m_minimapsWanted; }
  int minimapsLoaded() const { return m_minimapsLoaded; }
  int minimapsMissing() const { return m_minimapsMissing; }
  bool minimapsLoading() const { return !m_minimapQueue.empty() || !m_minimapTickets.empty(); }
  double minimapsMs() const { return m_minimapsMs; }
  // Grows with every picture that comes (the overview's mosaic follows it), and with every map.
  int imagesSerial() const { return m_imagesSerial; }
  // A picture came: the overhead map repaints (nothing else of the viewer is updated for it).
  std::function<void()> onImagesChanged;
  // A world-model-only map's global WMO, from its table: records (0: none), and the first one's file.
  int globalWmoRecords() const { return m_globalWmoRecords; }
  quint32 globalWmoFileDataID() const { return m_globalWmoFileDataID; }

  // A WMO-ONLY MAP (protocol 16): the map is one world model and no tile (the WDT's kind GlobalWmo), and its footprint.
  enum class FootprintPhase
  {
    None,           // not a WMO-only map, or not known yet
    WaitingPlayer,  // the player is not ready (or too old) to read the world model
    Reading,        // the world model's root is being read by the map asset worker
    Inspecting,     // the player is reading it
    Ready,          // the footprint is known
    Failed,         // the root could not be read or inspected (detail says why)
    Unsupported,    // the WDT names its world model by a path only (an old-form WDT): not entered (detail says why)
  };
  bool worldModelMap() const { return m_mapPhase == MapPhase::Ready && m_globalOnly; }
  FootprintPhase footprintPhase() const { return m_footprintPhase; }
  QString footprintDetail() const { return m_footprintDetail; }
  bool hasFootprint() const { return worldModelMap() && m_footprintPhase == FootprintPhase::Ready; }
  // The footprint, world frame (yards; x north, y west, z up): the placed root box (min x, y, z, max x, y, z), and each
  // group's placed box (6 floats a group) with its MOGI flags (0x2000: interior).
  const double * footprintRootBox() const { return m_footprintRoot; }
  int footprintGroups() const { return (int)m_footprintFlags.size(); }
  int footprintInteriorGroups() const { return m_footprintInterior; }
  const std::vector<float> & footprintGroupBoxes() const { return m_footprintBoxes; }
  const std::vector<quint32> & footprintGroupFlags() const { return m_footprintFlags; }
  // The world model on screen (a WMO-only map's world load shown).
  bool worldModelShown() const { return m_shownGlobal; }
  // The world's state in words: its world models while they load, and their outcome (empty: no world load).
  QString worldDetail() const { return m_worldDetail; }
  // Protocol 17: the doodads' outcome in words, one line (empty until a world load is reported); with a protocol-16 player,
  // why there are none.
  QString doodadDetail() const { return m_doodadDetail; }
  // The last progress of the current world load (load 0: none).
  const UnityIpcServer::WorldProgress & worldProgress() const { return m_progress; }

  // WHAT THE GRID AND THE NOTICE SHOW.
  int mapId() const { return m_mapId; }
  QString mapName() const { return m_mapName; }
  QString mapDirectory() const { return m_mapDirectory; }
  MapPhase mapPhase() const { return m_mapPhase; }
  QString mapDetail() const { return m_mapDetail; }
  QString wdtDescription() const { return m_wdtDescription; }
  bool hasGrid() const { return m_mapPhase == MapPhase::Ready; }
  TileState tileState(int x, int y) const;
  quint32 tileRoot(int x, int y) const;
  quint32 tileTex0(int x, int y) const;
  int tilesInWdt() const { return m_tilesInWdt; }
  int tilesAvailable() const { return m_tilesAvailable; }
  int tilesCount(TileState s) const;
  // The tiles the WDT flags, as a box (for the grid's extent); false with none.
  bool tileExtent(int & minX, int & minY, int & maxX, int & maxY) const;
  TilePhase tilePhase() const { return m_tilePhase; }
  int tileX() const { return m_tileX; }
  int tileY() const { return m_tileY; }
  QString tileDetail() const { return m_tileDetail; }
  // The tile on screen (the player's last "built" one still shown), -1 for none.
  int shownTileX() const { return m_shownX; }
  int shownTileY() const { return m_shownY; }
  int shownMap() const { return m_shownMap; }
  QString shownView() const { return m_shownView; }
  QString viewDetail() const { return m_viewDetail; }
  // Why a tile cannot be loaded (TileState's words, with the file).
  QString tileReason(int x, int y) const;
  static QString stateName(TileState s);
  // A state line for the panel, and the viewport's notice while no tile is on screen (false: none -- the tile is shown).
  QString statusLine() const;
  bool viewportNotice(QString & title, QString & detail) const;
  const Timings & timings() const { return m_timings; }
  // Something shown changed: the panel repaints, the viewport's notice is decided again.
  std::function<void()> onChanged;
  // The current load (the terrain request it made), 0 for none.
  int load() const { return m_load; }
  int inspectionSerial() const { return m_inspection; }

private:
  void changed();
  void resetMap();
  void resetTile();
  void beginRead();
  void sendInspection();
  void buildGrid(const UnityIpcServer::MapTilesInspection & inspection);
  bool clientCurrent(int epoch, quint64 generation) const;
  UnityIpcServer * ipc() const;
  void startMinimaps(const UnityIpcServer::MapTilesInspection & inspection);
  void pumpMinimaps();
  void stopMinimaps();
  void onMinimapRead(int index, MapAssetWorker::Result & result);
  void readDeferredWdt();
  UnityIpcServer::WorldCamera flyCameraAtTarget();
  UnityIpcServer::WorldCamera flyCameraOnShownTile();
  bool targetOnShownTile() const;
  void applyCamera(const UnityIpcServer::WorldCameraApplied & applied);
  void leaveWorld();
  void beginWorldModelRead();
  void onWorldModelRead(MapAssetWorker::Result & result);
  void sendWorldModelInspection();
  UnityIpcServer::WorldCamera startCamera();
  // A world is on screen: a tile, or a WMO-only map's world model.
  bool worldOnScreen() const { return m_shownX >= 0 || m_shownGlobal; }
  QString progressLine(const UnityIpcServer::WorldProgress & p) const;
  // Protocol 17: the doodads' line of a world load's report (global: a WMO-only map's world model, no tile).
  QString doodadLine(const UnityIpcServer::TerrainTileReport & r, bool global) const;

  ModelViewer * m_viewer;
  bool m_active = false;
  int m_displayMode = 0;
  QString m_viewSent;            // the view the player was last told, "" none since it connected

  // The map: its identity, where its pick is, and the client it was made under.
  int m_mapId = -1;
  QString m_mapName;
  QString m_mapDirectory;
  MapPhase m_mapPhase = MapPhase::None;
  QString m_mapDetail;
  QString m_wdtDescription;
  int m_epoch = 0;
  quint64 m_generation = 0;
  int m_inspection = 0;          // the serial of the map's inspection (the worker's read and the player's table)
  int m_inspectionSerial = 0;    // the last one given
  quint64 m_readTicket = 0;
  bool m_readDeferred = false;   // the pick's read waits for an earlier read of this viewer's to come back
  QByteArray m_wdtBytes;         // the WDT read, until the player has its table (then dropped)
  QString m_wdtMd5;
  int m_wdtFileDataID = 0;
  QElapsedTimer m_pickClock, m_inspectClock, m_loadClock, m_viewClock;
  std::vector<unsigned char> m_state;   // Tiles * Tiles TileState, index y * 64 + x
  std::vector<quint32> m_roots, m_tex0s;
  int m_tilesInWdt = 0;
  int m_tilesAvailable = 0;

  // The tile.
  TilePhase m_tilePhase = TilePhase::None;
  int m_tileX = -1, m_tileY = -1;
  QString m_tileDetail;
  int m_load = 0;
  int m_shownX = -1, m_shownY = -1, m_shownMap = -1;
  QString m_shownView;
  QString m_viewDetail;
  Timings m_timings;

  // The target.
  bool m_hasTarget = false;
  double m_targetX = 0.0, m_targetY = 0.0, m_targetZ = 0.0;
  bool m_targetHasZ = false;

  // The world and its camera.
  bool m_worldShown = false;     // the world has the viewport (a camera was placed after Enter world)
  bool m_entering = false;       // Enter world asked, its camera not answered yet
  CameraMode m_cameraMode = CameraMode::Fly;
  int m_cameraSerial = 0;        // the last request given
  int m_cameraRequest = 0;       // the request whose answer counts
  QString m_cameraDetail;
  bool m_cameraPlaced = false;
  bool m_cameraHeld = false;     // the answer to the camera a load carried came before the load's report: held for it
  UnityIpcServer::WorldCameraApplied m_heldCamera;
  double m_cameraX = 0.0, m_cameraY = 0.0, m_cameraZ = 0.0;
  double m_lookYaw = 0.0, m_lookPitch = -8.0;

  // The pictures.
  std::vector<quint32> m_minimapIds;           // MAID word 7 per tile (empty without MAID)
  std::vector<QByteArray> m_thumbs;            // Tiles * Tiles, empty until a picture came
  std::vector<unsigned char> m_thumbState;     // MinimapState per tile (MapsViewer.cpp)
  std::vector<QString> m_thumbReason;          // why a tile has no picture
  std::deque<int> m_minimapQueue;              // tile indices still to read, nearest the middle first
  std::map<quint64, int> m_minimapTickets;     // the worker's tickets of the reads out, and their tiles
  wxTimer m_minimapTimer;                      // gives the next batch (MinimapPumpMs)
  int m_minimapsWanted = 0, m_minimapsLoaded = 0, m_minimapsMissing = 0;
  int m_imagesSerial = 0;
  QElapsedTimer m_minimapClock;
  double m_minimapsMs = -1.0;
  int m_globalWmoRecords = 0;
  quint32 m_globalWmoFileDataID = 0;
  QString m_globalWmoPath;

  // A WMO-only map (protocol 16): its record, its root read and the footprint the player made of it.
  bool m_globalOnly = false;
  UnityIpcServer::MapTilesInspection::GlobalRecord m_globalRecord;
  FootprintPhase m_footprintPhase = FootprintPhase::None;
  QString m_footprintDetail;
  quint64 m_wmoReadTicket = 0;
  QByteArray m_wmoBytes;         // the root read, until the player has inspected it (then dropped)
  QString m_wmoMd5;
  QElapsedTimer m_footprintClock;
  double m_footprintRoot[6] = { 0, 0, 0, 0, 0, 0 };
  std::vector<float> m_footprintBoxes;
  std::vector<quint32> m_footprintFlags;
  int m_footprintInterior = 0;
  // The world load: shown (worldShown came), the world model on screen, its progress and outcome.
  bool m_loadShown = false;
  bool m_shownGlobal = false;
  UnityIpcServer::WorldProgress m_progress;
  QString m_worldDetail;
  QString m_doodadDetail;
};

#endif // MAPSVIEWER_H
