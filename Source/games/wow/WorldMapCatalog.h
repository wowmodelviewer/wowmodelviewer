/*
 * WorldMapCatalog.h
 *
 * THE WORLD MAP CATALOGUE: the maps of the loaded client as its own Map.db2 lists them, each with its WDT reference
 * resolved the way that client resolves it, and -- per map, on request -- whether that WDT can be read on this computer.
 *
 * It reads no WDT's contents. A WDT, its tiles and every ADT are the Unity player's to parse (the verified parsers in
 * Tools/UnityRendererProject/Assets/Scripts/Wow/World, docs/world-viewer/01-parsers.md); the catalogue only says which
 * file a map's WDT is and whether the client can supply it. Evidence for every field and rule:
 * docs/world-viewer/02-map-catalog.md.
 *
 * One catalogue belongs to one loaded client. The viewer builds it as a client loads and drops it when another client
 * loads (ModelViewer::LoadWoW / ResetClientState). Something that kept one past that finds belongsToActiveClient() false,
 * and inspect() refuses: a catalogue never reads the storage of a client it was not built from. GUI thread only, like
 * the storage it reads.
 */

#ifndef _WORLDMAPCATALOG_H_
#define _WORLDMAPCATALOG_H_

#include <memory>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WORLDMAPCATALOG_API_ __declspec(dllexport)
#    else
#        define _WORLDMAPCATALOG_API_ __declspec(dllimport)
#    endif
#else
#    define _WORLDMAPCATALOG_API_
#endif

namespace wow
{
  // The client a catalogue was built from.
  struct MapCatalogClient
  {
    QString product;            // .build.info product code: wow, wow_classic, wow_classic_era, wow_classic_beta, ...
    QString build;              // the client's version, e.g. 12.1.0.69933
    QString locale;             // the locale its storage was opened with (Map.db2's strings are in it)
    QString dataPath;           // the installation's Data folder
    QString schema;             // the schema folder its database was read with, e.g. games/wow/12.0/
    quint64 storageGeneration = 0; // the storage it was built from (CASCFolder::storageGeneration)
    qint64 rootNames = 0;       // files its storage root knows by name
    qint64 rootNameless = 0;    // files its storage root lists by FileDataID only (a path cannot find them)
  };

  // A code a client's Map.db2 stores for a map, kept as stored. Absent when the client's layout has no such column --
  // never read as 0.
  struct MapCode
  {
    bool present = false;
    int value = 0;
  };

  // How a map's WDT is found. Exactly one way per map, never a guess: Map.db2's own FileDataID where the client's
  // layout has the column, else the client's directory rule.
  struct WdtReference
  {
    enum class Kind
    {
      Unresolved, // the client gives this map no WDT (why says how)
      FileDataId, // Map.db2's WdtFileDataID column (Retail, Classic Beta)
      Path,       // world/maps/<Directory>/<Directory>.wdt, found through the storage root's own names (Classic Era, MoP Classic)
    };
    Kind kind = Kind::Unresolved;
    int fileDataId = 0;  // Kind::FileDataId only
    QString path;        // Kind::Path only
    QString why;         // Kind::Unresolved only
  };

  // Whether a map's WDT can be read here. Each is its own fact: a map can be in Map.db2 while its WDT is not in the
  // client's storage; a WDT the storage lists may not be downloaded; a downloaded one may fail to read.
  enum class WdtAvailability
  {
    NotChecked,          // not inspected yet (inspection is lazy)
    Available,           // listed by the storage root, on this computer, and every byte read and checked against its
                         // content key
    NotInClientStorage,  // the client's storage root does not list it (by path: only said when the root names every
                         // file it lists)
    NotInstalledLocally, // listed by the root, but its data is not on this computer (a partly downloaded client)
    Unreadable,          // on this computer, but it could not be read (encrypted without a key, damaged, empty)
    Unsupported,         // nothing to inspect: no WDT reference, or a path the root cannot answer for (it has no name
                         // for it, but lists files without names)
  };

  struct WdtInspection
  {
    WdtAvailability state = WdtAvailability::NotChecked;
    QString detail;              // the precise reason for every state but Available
    int storageFileDataId = -1;  // the FileDataID the storage root gives the WDT; -1 when it is not listed
    qint64 bytes = -1;           // Available: the WDT's size
    QString sha256;              // Available, with hashing on: the SHA-256 of the bytes read (hex), to compare with a known copy
    double milliseconds = 0.0;   // how long the inspection took
  };

  // The form of a map's WDT as Map.db2 states it (its MapType code), before the WDT is read. The WDT itself is the
  // authority -- the Unity player's parser decides what it holds (docs/world-viewer/01-parsers.md). The codes match
  // every WDT the four audited clients can read (docs/world-viewer/02-map-catalog.md section 4).
  enum class StatedWdtForm
  {
    Unknown,   // no MapType column, or a code the evidence does not cover
    TileGrid,  // MapType 1: a tile-grid WDT with no global world model -- usually with tiles (13 Retail ones have none)
    WmoOnly,   // MapType 2 (modern form: the world model by FileDataID) or 4 (old form: by MWMO path, Retail only)
    NoWdt,     // MapType 3: no WDT this client can read (Map.db2 names none, or one its storage root does not list)
  };

  struct _WORLDMAPCATALOG_API_ MapEntry
  {
    int id = -1;               // Map.db2 ID: the map's identity
    QString directory;         // Map.db2 Directory: the internal name, and the WDT's folder
    QString name;              // Map.db2's name column: the name players see (where the client fills it)
    // Classification as the client stores it: codes, not a taxonomy (grouping is the Maps viewer's, later). What each
    // code is established to mean, client by client: docs/world-viewer/02-map-catalog.md.
    MapCode mapType;           // the form of the map's WDT (see StatedWdtForm)
    MapCode instanceType;      // the code Difficulty.db2 gives the map's difficulties (1, 2, 5 shown; others unlabelled)
    MapCode parentMapId;       // when set: a map whose world space this one shares (-1: not set -- not "no parent")
    MapCode worldCopyOfMapId;  // when set: a map part of whose world this one copies (-1: not set); absent in Classic Beta
    MapCode locationMapId;     // when set: the map an instance is located on (-1: not set; Classic Era and Beta also store
                               // 0 with no point, which reads like map 0)
    WdtReference wdt;
    WdtInspection inspection;

    StatedWdtForm statedWdtForm() const;
  };

  class _WORLDMAPCATALOG_API_ WorldMapCatalog
  {
  public:
    enum class Status
    {
      NoClient,            // no client is loaded
      Unsupported,         // the loaded client has no Map.db2 this viewer reads (a legacy MPQ client)
      MapTableUnavailable, // Map.db2 is not readable for this client (detail says why: not installed, layout not
                           // verified, not in the client, query failed)
      Ready,
    };

    // How the loaded client's Map.db2 names its WDTs, from its own layout (the schema check), never from the product.
    enum class WdtRule
    {
      None,             // no catalogue
      ByFileDataId,     // the layout has the WdtFileDataID column
      ByDirectoryPath,  // the layout has no such column: world/maps/<Directory>/<Directory>.wdt
    };

    // The catalogue of the client loaded now. Never fails: one that could not be built says why (status()).
    static std::shared_ptr<WorldMapCatalog> build();
    // A catalogue of no client, with the reason (before any client is loaded, while one loads): what the viewer hands
    // out instead of nothing, so a holder always gets a status.
    static std::shared_ptr<WorldMapCatalog> noClient(const QString & why);

    Status status() const { return m_status; }
    QString statusDetail() const { return m_statusDetail; }
    const MapCatalogClient & client() const { return m_client; }
    // Is the client it was built from still the loaded one (same storage, product, build and installation)?
    bool belongsToActiveClient() const;

    WdtRule wdtRule() const { return m_rule; }
    QString mapLayout() const { return m_layout; }     // Map.db2's layout hash in this client
    qint64 lockedRows() const { return m_lockedRows; } // Map.db2 records in sections encrypted without a key here
    const std::vector<MapEntry> & maps() const { return m_maps; }
    const MapEntry * find(int mapId) const;

    // Opens and reads one map's WDT through the client's storage now, and keeps only the outcome (entry.inspection).
    // False -- the storage untouched, every entry as it was, the reason in lastRefusal() -- when the catalogue is not
    // ready or no longer belongs to the loaded client, or there is no such map.
    bool inspect(int mapId);
    // Every map's; how many were inspected (0 when refused). Reads every WDT: for tests and on-demand use, never as
    // part of loading a client.
    size_t inspectAll();
    QString lastRefusal() const { return m_refusal; }
    // Inspections also hash what they read (WdtInspection::sha256): off by default, for tests that compare the bytes.
    void setHashing(bool on) { m_hashing = on; }
    // The same inspection for any file of this catalogue's client, by FileDataID or by path (a tile's parts, a global
    // world model): listed by the root, on this computer, and read in full; with bytes given, they receive the file
    // when it is Available. Refused -- state NotChecked, the reason in detail and in lastRefusal() -- when the catalogue
    // is not ready or no longer belongs to the loaded client. No check of what the file is: that is the caller's. A file
    // larger than maxBytes is not read: Unreadable, the detail naming its size and the limit.
    WdtInspection inspectFile(const WdtReference & reference, QByteArray * bytes = nullptr,
                              qint64 maxBytes = 64 * 1024 * 1024);
    // A map's WDT bytes, read through the client's storage by the map's own reference -- what a runtime hands the
    // player's WDT parser. False, with why, when the catalogue no longer belongs to the loaded client, the map is not
    // in it, it has no WDT reference, or the WDT cannot be read (the inspection's reason). Updates the map's inspection.
    bool readWdt(int mapId, QByteArray & bytes, QString & why);

    // Costs, for the load log and the tests.
    double queryMilliseconds() const { return m_queryMs; }
    double buildMilliseconds() const { return m_buildMs; }
    size_t approximateBytes() const;

    // Everything above as JSON (the headless catalogue test writes it): the client, the status, Map.db2's reading,
    // and every map with its reference and, once inspected, its availability.
    QJsonObject toJson() const;

    static QString availabilityName(WdtAvailability a);
    static QString statusName(Status s);

  private:
    WorldMapCatalog() {}
    void inspectEntry(MapEntry & entry, QByteArray * bytes = nullptr) const;
    // May this catalogue read its client's storage now (ready, and still the loaded client's)? Else the reason.
    bool canRead();
    QString refusalReason() const;
    static WdtInspection inspectReference(const WdtReference & reference, QByteArray * bytes, bool hash, qint64 maxBytes);

    Status m_status = Status::NoClient;
    QString m_statusDetail;
    MapCatalogClient m_client;
    WdtRule m_rule = WdtRule::None;
    QString m_layout;
    QString m_layoutState;
    QStringList m_absentColumns;
    qint64 m_rowsInTable = -1;
    qint64 m_lockedRows = 0;
    std::vector<MapEntry> m_maps;
    double m_queryMs = 0.0;
    double m_buildMs = 0.0;
    QString m_refusal;
    bool m_hashing = false;
  };
}

#endif /* _WORLDMAPCATALOG_H_ */
