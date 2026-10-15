/*
 * MapsViewer.h
 *
 * THE MAPS VIEWER (docs/world-viewer/05-maps-viewer-ui.md): the fourth viewer mode's state, between the user's choices
 * (a map in Browse, a tile in the tile grid, a display mode) and the verified world pipeline of stages 02-04B. It owns
 * no widget: Browse lists the catalogue's maps (FileControl), the tile grid draws this state (MapTilePanel), and the
 * viewport shows the tile (the Unity player) or the notice this state names.
 *
 *   a map picked     its WDT read whole by the MAP ASSET WORKER (off the GUI thread; the read, its frame checks and its
 *                    content key are the catalogue's own, WorldMapCatalog::inspectInStorage), then handed to the player
 *                    whose verified parser returns the tile table (inspectMapTiles -> mapTilesInspected, protocol 14):
 *                    the host never parses a WDT. Each tile's root ADT (MAID word 0) is then looked up in the client's
 *                    storage root: listed, on this computer (never read). A tile is available only when its MAIN flag is
 *                    set, MAID names a root ADT, and the storage lists that file and has it locally.
 *   a tile clicked   an available one is requested (ModelViewer::RequestTerrainTile: Stage 03/04 terrain, protocol 13
 *                    serving, the Stage 04B worker), with the WDT inspection the worker made, so the GUI thread reads
 *                    nothing; any other says why it cannot be loaded.
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

#include <functional>
#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QString>

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

  // THE VIEWER MODE: entered (the grid comes back as it was left, for the same client), left (the terrain cleared).
  void Entered();
  void Left();
  bool active() const { return m_active; }

  // A CLIENT: being replaced (everything about the old one forgotten now; its reads cancelled by the server), loaded.
  void ClientReplaced();
  void ClientLoaded();

  // THE USER'S CHOICES.
  void SelectMap(int mapId);
  // A tile clicked in the grid: an available one is loaded; any other is picked, with why it cannot be.
  void SelectTile(int x, int y);
  void ClearTerrain();
  void SetDisplayMode(int index);
  int displayMode() const { return m_displayMode; }

  // THE PLAYER'S AND THE WORKER'S ANSWERS (wired by ModelViewer to its server).
  void OnPlayerReady();
  void OnWdtRead(MapAssetWorker::Result & result);
  void OnTilesInspected(const UnityIpcServer::MapTilesInspection & inspection);
  void OnTerrainTileLoaded(const UnityIpcServer::TerrainTileReport & report);
  void OnTerrainViewApplied(const UnityIpcServer::TerrainViewApplied & applied);

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
};

#endif // MAPSVIEWER_H
