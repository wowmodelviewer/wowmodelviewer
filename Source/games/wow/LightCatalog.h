/*
 * LightCatalog.h
 *
 * THE LIGHT CATALOGUE: the lights of the loaded client as its own light tables describe them, for the water colour of the
 * zone the camera is in -- the zone light polygons (ZoneLight, ZoneLightPoint), the lights (Light: position, falloff, map,
 * the clear-weather LightParams of slot 0) and each light's water colours and alphas at 12:00 (LightParams, LightData),
 * with one sky colour for the water's reflections that find no scene (Stage 09; the sky Stage 10 draws comes from the
 * outdoor light below, the water keeps this object).
 * Built once as the client loads, from the database (database.xml and dbd/Light.dbd, LightParams.dbd, LightData.dbd,
 * ZoneLight.dbd, ZoneLightPoint.dbd, each table read only from a verified layout), every table read whole once; the object
 * a map's loads carry (mapJson) is made on the map's first load and kept. Evidence and rules: docs/world-viewer/09-liquids.md.
 *
 * The Unity player picks the light at its camera itself (the first zone polygon, in the order sent, that holds the camera,
 * else the map's default light; the point lights blended over it): the host sends the selection data only, inside the
 * "liquids" offer of a terrain or world load (protocol 19, UnityIpcServer.h LIQUIDS, "lights"). Every table value is the
 * client's own (PROVEN_BYTES per column, in the dbd files); how the client selects and blends lights is not established, and the
 * rules here (the polygon order, which lights are point lights, the default light, 12:00 and slot 0) are INFERRED. The
 * catalogue reads one file itself: LightParams.db2's copy table, since the database keeps a copy row under its own ID only
 * and a LightParams whose day has no LightData keys of its own takes its copy source's.
 *
 * THE OUTDOOR LIGHT (Stage 10, docs/world-viewer/10-light-sky-fog.md 5 and 6.2-6.3): beside all of the above, which it
 * leaves as it is, a typed store of every named LightData column of every key (24 colours, 26 floats as the client stores
 * them, 2 FileDataIDs), the eight LightParams slots of every Light, LightParams' glow, skybox, flags and sun overrides,
 * ZoneLight.TransitionType and Map.TimeOfDayOverride and Flags -- read once as the client loads (buildWorldLight, a stage of
 * its own after build()), with one JSON fragment per LightParams day made then. A map's "worldLight" object
 * (worldLightJson, protocol 20, UnityIpcServer.h OUTDOOR LIGHT) is assembled from those fragments on the map's first load
 * and kept: Stage 09's zones (with their TransitionType), points and default light, every light they name with its eight
 * slots, the LightParams of slots 0 and 1 and their whole days. The host sends whole days; the player selects, evaluates
 * and eases (6.1). Validity is the player's, per group of columns; the water keeps "lights" and its rule.
 *
 * One catalogue belongs to one loaded client, like the liquid catalogue (LiquidCatalog): the viewer builds it as a client
 * loads and drops it when another client loads; one kept past that finds belongsToActiveClient() false. GUI thread only.
 */

#ifndef _LIGHTCATALOG_H_
#define _LIGHTCATALOG_H_

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _LIGHTCATALOG_API_ __declspec(dllexport)
#    else
#        define _LIGHTCATALOG_API_ __declspec(dllimport)
#    endif
#else
#    define _LIGHTCATALOG_API_
#endif

namespace wow
{
  class _LIGHTCATALOG_API_ LightCatalog
  {
  public:
    enum class Status
    {
      NoClient,     // no client is loaded
      Unavailable,  // the client's light tables could not be read (statusDetail says why)
      Ready,
    };

    // The time of day and the LightParams slot sent: 12:00 (half-minute 1440 of 2880) and slot 0, clear weather (the slot's
    // meaning is the public definitions'). One fixed time is Stage 09's choice for the water; the day cycle is Stage 10's
    // (worldLightJson sends whole days, the player keeps the clock).
    static const int TIME = 1440;
    static const int SLOT = 0;
    static const int DAY = 2880;

    // How one of the five tables was read for the client (the database's schema check), and the rows it holds here (copy
    // rows included; -1 when it could not be counted).
    struct Table
    {
      QString name;
      QString layout;           // the client's layout hash of it (8 hex digits); empty when the client has no such file
      QString state;            // "verified", or why it was not read
      bool read = false;        // read from a verified layout
      qint64 rows = -1;
      qint64 lockedRows = 0;    // rows in sections encrypted with a key this computer lacks (not read)
      QStringList absentFields; // columns this client's layout lacks (each read as 0)
    };

    // One LightData key of a LightParams' day: its half-minute, the four water colours and the sky colour, 0xRRGGBB (masked
    // to 24 bits).
    struct Key
    {
      int time = 0;
      quint32 riverClose = 0;
      quint32 riverFar = 0;
      quint32 oceanClose = 0;
      quint32 oceanFar = 0;
      quint32 sky = 0;          // SkyBand1Color: what a water reflection that finds no scene shows (dbd/LightData.dbd)
    };
    // The colours at one time of day, (r, g, b) from 0 to 255 each: river close, river far, ocean close, ocean far (the
    // four water colours), then the sky (SKY).
    static const int SKY = 4;
    struct Water
    {
      int colour[5][3] = {};
    };

    // What the build found in the tables (whole tables, every map), for the harness and the log.
    struct Counts
    {
      int keysOutsideDay = 0;        // LightData keys whose Time (masked to 16 bits) is DAY or more: left out
      int keysRepeated = 0;          // keys whose time repeats an earlier key of the same LightParams: the first by ID kept
      qint64 coloursMasked = -1;     // keys with a water colour whose stored value had bits above the 24 (signed storage)
      int alphasOutsideRange = 0;    // LightParams rows with an alpha outside [0, 1]
      int pointsWithoutZone = 0;     // ZoneLightPoint rows whose ZoneLightID is no ZoneLight row
      int zonesWithoutPoints = 0;    // ZoneLight rows with fewer than 3 points (never sent)
      int polygonsOutOfOrder = 0;    // polygons whose PointOrder does not run 1..n
      int zonesWithoutLight = 0;     // ZoneLight rows whose LightID is no Light row
      int lightsWithoutSlot = 0;     // Light rows whose slot-0 LightParamsID is 0
      int lightsWithoutParams = 0;   // ... whose slot-0 LightParams is no LightParams row
      int lightsWithoutKeys = 0;     // ... whose slot-0 LightParams has no LightData keys, nor its copy source
      int lightsByCopySource = 0;    // ... whose slot-0 LightParams has no keys of its own and takes its copy source's
      int copyRows = -1;             // LightParams.db2's copy rows (-1: its copy table could not be read)
    };

    // The catalogue of the client loaded now. Never fails: one that could not be built says why (status()).
    static std::shared_ptr<LightCatalog> build();
    // A catalogue of no client, with the reason (before any client is loaded, while one loads).
    static std::shared_ptr<LightCatalog> noClient(const QString & why);

    Status status() const { return m_status; }
    QString statusDetail() const { return m_statusDetail; }
    QString clientProduct() const { return m_product; }
    QString clientBuild() const { return m_build; }
    // Is the client it was built from still the loaded one (same storage, product, build and installation)?
    bool belongsToActiveClient() const;

    const std::vector<Table> & tables() const { return m_tables; }
    const Table * table(const QString & name) const;
    const Counts & counts() const { return m_counts; }
    double buildMilliseconds() const { return m_buildMs; }

    // The lights of one map as the player is sent them ("lights" of a load's "liquids", UnityIpcServer.h LIQUIDS): product,
    // build, state "ok", map, time, slot, the default light, the zone polygons in the order the player tries them, the
    // point lights and each light's water and sky at TIME. Made on the map's first request and kept; a catalogue that is
    // not Ready gives unavailableJson() with its reason.
    QJsonObject mapJson(int mapId) const;
    // The same object for a load that has no light catalogue of the loaded client: state "unavailable" with why, no zones,
    // points or values (the player then draws the liquid catalogue's colours).
    static QJsonObject unavailableJson(const QString & product, const QString & build, int mapId, const QString & why);
    // The object in brief, for a log line: "lights ok (map 1: 25 zones, 111 points, 137 values, default light 191 end0)".
    static QString describe(const QJsonObject & lights);
    // A day's keys (sorted by time, each time once, all below DAY) at half-minute t: the key at t, else linear between the
    // keys before and after it, wrapping at DAY; one key alone holds all day. Each channel of the five colours rounded to
    // the nearest integer. False without keys.
    static bool sample(const std::vector<Key> & keys, int t, Water & out);
    static QString statusName(Status s);

    // THE OUTDOOR LIGHT (see the top). The Light slots read (LightParamsID[8] in every layout: PROVEN_BYTES; the public
    // meanings 0 clear, 1 clear underwater, 2 storm, 3 storm underwater, 4 death, 5-7 not known: INFERRED (public)) and
    // the slots whose LightParams' days a map's object carries (0 and 1: the plan's choice, docs/world-viewer/
    // 10-light-sky-fog.md 6.3; the others are sent as IDs only).
    static const int SLOTS = 8;
    static const int DAY_SLOTS = 2;
    // The column order of every day sent ("colorColumns", "floatColumns", "intColumns"): the public names, as
    // dbd/LightData.dbd places them per layout.
    static const QStringList & worldLightColourColumns();   // 24, each 0xRRGGBB (masked to 24 bits)
    static const QStringList & worldLightFloatColumns();    // 26, each the float the client stores
    static const QStringList & worldLightIntColumns();      // 2 (the colour-grading FileDataIDs)
    // What buildWorldLight found (whole tables, every map), for the harness and the log.
    struct WorldCounts
    {
      int days = 0;                  // LightParams with keys of their own
      qint64 keys = 0;               // keys kept (within the day, each time once a day: Stage 09's rule)
      int keysOutsideDay = 0;        // keys whose Time (16 bits) is DAY or more: left out
      int keysRepeated = 0;          // keys whose time repeats an earlier key of the same LightParams: the first by ID kept
      qint64 coloursMasked = -1;     // keys with a colour (any of the 24) whose stored value had bits above the 24
      int floatsNotFinite = 0;       // float values that are not finite (sent as 0)
      int lightsWithSlot[SLOTS] = {};  // Light rows whose slot is not 0, per slot
      int lightsWithoutDay[DAY_SLOTS] = {};  // ... whose slot 0 / 1 LightParams has no keys, nor its copy source (slot 1:
                                             // the player falls back to slot 0, INFERRED)
      int params = 0;                // LightParams rows (copy rows included)
      std::map<int, int> zonesByPriority;   // ZoneLight rows by TransitionType (0 where the layout has none)
      int maps = 0;                  // Map rows read
      int mapsOverridden = 0;        // ... whose TimeOfDayOverride is not -1
      int overrideMax = -1;          // ... the largest of those
      int mapsFlag2Bit1 = 0;         // ... whose Flags word 2 has bit 0x2
      double readMs = 0.0;           // the queries and their parsing
      double fragmentMs = 0.0;       // the day fragments
    };
    // Reads the outdoor light's store and makes the day fragments (once, as the client loads, after build(); its own stage
    // of the client load). A catalogue that is not Ready reads nothing; one whose store could not be read says why
    // (worldLightDetail) and its maps' objects are "unavailable".
    void buildWorldLight();
    bool worldLightReady() const { return m_worldReady; }
    QString worldLightDetail() const { return m_worldDetail; }
    const WorldCounts & worldCounts() const { return m_worldCounts; }
    double worldLightBuildMilliseconds() const { return m_worldBuildMs; }
    // A map's "worldLight" object as the player is sent it (UnityIpcServer.h OUTDOOR LIGHT): made on the map's first request
    // and kept (its assembly time: worldLightAssemblyMilliseconds, -1 before); "unavailable" with why when the catalogue or
    // its store is not ready.
    QJsonObject worldLightJson(int mapId) const;
    double worldLightAssemblyMilliseconds(int mapId) const;
    // The same object made afresh and NOT kept, and the time its assembly took (ms, when asked): the harness's census of
    // every map's object and its second timing of an assembly. "unavailable" as worldLightJson.
    QJsonObject assembleWorldLightJson(int mapId, double * ms = nullptr) const;
    // The same object for a load without a ready catalogue: state "unavailable", the same keys, empty lists.
    static QJsonObject worldLightUnavailableJson(const QString & product, const QString & build, int mapId, const QString & why);
    // The object in brief, for a log line.
    static QString describeWorldLight(const QJsonObject & worldLight);
    // The client's shader branch (docs/world-viewer/10-light-sky-fog.md 4.0): "ptr" for wowt and wow_classic_beta and for
    // any 12.1.7-or-later or 1.60-or-later build, else "retail" (wow, wow_classic_era, wow_classic).
    static QString branchOf(const QString & product, const QString & build);

  private:
    LightCatalog() {}
    QJsonObject makeMapJson(int mapId) const;
    QJsonObject makeWorldLightJson(int mapId) const;

    struct Light
    {
      double x = 0.0, y = 0.0, z = 0.0;   // GameCoords: yards, game X north, Y west, Z up
      double start = 0.0, end = 0.0;      // GameFalloffStart / End, yards
      int map = 0;                        // ContinentID
      int params = 0;                     // LightParamsID[SLOT]
    };
    struct Zone
    {
      int id = 0;
      int map = 0;
      int light = 0;
      double zmin = 0.0, zmax = 0.0;
      bool conditional = false;           // PlayerConditionID != 0
      std::vector<double> poly;           // (x, y) per corner in PointOrder, flat
      double area = 0.0;                  // the polygon's area (square yards), whatever its winding
    };
    struct Alphas
    {
      double waterShallow = 0.0, waterDeep = 0.0, oceanShallow = 0.0, oceanDeep = 0.0;
    };
    // A map's lights as the player tries them (Stage 09's rules, makeMapJson): its zones in order, its point lights (by
    // ID), its default light, and every light those name.
    struct Selection
    {
      std::vector<const Zone *> zones;
      std::vector<int> points;
      int defaultLight = 0;
      QString defaultFrom = "none";
      std::set<int> used;
    };
    Selection select(int mapId) const;
    QJsonObject zoneJson(const Zone & z) const;
    QJsonObject pointJson(int id, const Light & l) const;
    // The outdoor light's store: one LightParams day (its keys sorted by time, each time once), every named column of each
    // key in the order of the column lists, row-major, and the fragment sent for it. Only the fragment is kept: the four
    // vectors are buildWorldLight's own, released once the fragment is made (every later read is of the fragment).
    struct Day
    {
      std::vector<int> times;
      std::vector<quint32> colours;   // keys x 24
      std::vector<float> floats;      // keys x 26
      std::vector<quint32> ints;      // keys x 2
      QJsonObject fragment;           // { "times", "c", "f", "i" }
    };
    struct Params
    {
      float glow = 0.0f;
      int skybox = 0;
      quint32 flags = 0;
      int highlightSky = 0;
      float overrideSun[3] = {0.0f, 0.0f, 0.0f};
      float sunPolar = 0.0f, sunAzimuth = 0.0f;
    };
    struct MapLight
    {
      int timeOverride = -1;
      quint32 flags[3] = {0, 0, 0};
    };

    Status m_status = Status::NoClient;
    QString m_statusDetail;
    QString m_product;
    QString m_build;
    QString m_dataPath;
    quint64 m_storageGeneration = 0;
    std::vector<Table> m_tables;
    Counts m_counts;
    double m_buildMs = 0.0;
    std::map<int, Light> m_lights;              // Light by ID
    std::map<int, Alphas> m_alphas;             // LightParams by ID (copy rows included)
    std::map<int, std::vector<Key>> m_keys;     // LightData keys by LightParamID, sorted by time
    std::map<int, int> m_copySource;            // LightParams copy row ID -> the ID it copies
    std::vector<Zone> m_zones;                  // every ZoneLight row with 3 points or more, every map
    std::set<int> m_zoneLights;                 // every ZoneLight row's LightID: never a point light
    mutable std::map<int, QJsonObject> m_maps;  // mapJson's objects, by map
    // The outdoor light (buildWorldLight).
    bool m_worldReady = false;
    QString m_worldDetail = "the outdoor light was not read";
    WorldCounts m_worldCounts;
    double m_worldBuildMs = 0.0;
    QString m_lightDataLayout;
    std::map<int, Day> m_days;                       // by LightParamID
    std::map<int, Params> m_params;                  // LightParams by ID (copy rows included)
    std::map<int, std::vector<int>> m_slots;         // Light by ID: its SLOTS LightParams IDs as stored (0: none)
    std::map<int, int> m_zonePriority;               // ZoneLight by ID: its TransitionType
    std::map<int, MapLight> m_mapLights;             // Map by ID
    QStringList m_absentColumns;                     // the named LightData columns this client's layout lacks
    mutable std::map<int, QJsonObject> m_worldMaps;  // worldLightJson's objects, by map
    mutable std::map<int, double> m_worldMapMs;      // ... and the time each took to assemble
  };
}

#endif /* _LIGHTCATALOG_H_ */
