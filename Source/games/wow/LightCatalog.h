/*
 * LightCatalog.h
 *
 * THE LIGHT CATALOGUE: the lights of the loaded client as its own light tables describe them, for the water colour of the
 * zone the camera is in -- the zone light polygons (ZoneLight, ZoneLightPoint), the lights (Light: position, falloff, map,
 * the clear-weather LightParams of slot 0) and each light's water colours and alphas at 12:00 (LightParams, LightData),
 * with one sky colour for the water's reflections that find no scene (no sky is drawn before Stage 12).
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
    // meaning is the public definitions'). One fixed time is Stage 09's choice; the day cycle is Stage 12's.
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

  private:
    LightCatalog() {}
    QJsonObject makeMapJson(int mapId) const;

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
  };
}

#endif /* _LIGHTCATALOG_H_ */
