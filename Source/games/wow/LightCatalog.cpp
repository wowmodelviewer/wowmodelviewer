/*
 * LightCatalog.cpp
 *
 * See LightCatalog.h. The evidence for each column is in the dbd files (dbd/Light.dbd, LightParams.dbd, LightData.dbd,
 * ZoneLight.dbd, ZoneLightPoint.dbd) and docs/world-viewer/09-liquids.md.
 */

#include "LightCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <unordered_map>

#include <QElapsedTimer>
#include <QJsonArray>

#include "Game.h"
#include "GameDatabase.h"
#include "logger/Logger.h"
#include "wdc3file.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"

namespace
{
  // The tables as database.xml names them; all five must be read.
  const char * const kLightTable = "Light";
  const char * const kParamsTable = "LightParams";
  const char * const kDataTable = "LightData";
  const char * const kZoneTable = "ZoneLight";
  const char * const kPointTable = "ZoneLightPoint";

  wow::WoWFolder * activeWowFolder()
  {
    if (!core::Game::instance().initDone())
      return nullptr;
    return dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
  }

  bool readable(const wow::WoWDatabase::SchemaCheck::TableReading * reading)
  {
    using State = wow::WoWDatabase::SchemaCheck::TableReading::State;
    return reading && (reading->state == State::Verified || reading->state == State::VerifiedBySchemaLayout);
  }

  QString readingState(const wow::WoWDatabase::SchemaCheck::TableReading * reading)
  {
    if (!reading)
      return "not in the schema";
    using State = wow::WoWDatabase::SchemaCheck::TableReading::State;
    switch (reading->state)
    {
      case State::Verified: return "verified";
      case State::VerifiedBySchemaLayout: return "verified by the schema's layout";
      case State::KeptAtSchemaPositions: return "not verified (layout " + reading->layout + ")";
      case State::NotRead: return "not read: " + reading->reason;
      case State::NotInstalled: return "not installed on this computer";
      case State::NotInClient: return "not in this client";
    }
    return "unknown";
  }

  // The rows of one table, or -1 when it cannot be counted.
  qint64 countRows(const char * table)
  {
    const sqlResult r = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM %1").arg(table));
    if (!r.valid || r.values.empty() || r.values[0].empty())
      return -1;
    bool ok = false;
    const qint64 n = r.values[0][0].toLongLong(&ok);
    return ok ? n : -1;
  }

  // A table's copy rows from the client's own file (copy row ID -> the ID it copies): the database keeps a copy row as a
  // row of its own ID and nothing of its source. False when the file could not be read.
  bool readCopyRows(const QString & table, std::map<int, int> & out)
  {
    wow::TableStructure t;
    t.name = table;
    t.file = table;
    std::unique_ptr<DBFile> file(t.createDBFile());
    WDC3File * wdc = dynamic_cast<WDC3File *>(file.get());
    if (!wdc || !wdc->open())
      return false;
    for (const std::pair<uint32, uint32> & c : wdc->copyRows())
      out[(int)c.first] = (int)c.second;
    return true;
  }

  // The shoelace area of a flat (x, y) polygon, whatever its winding (the data's is mixed: PROVEN_BYTES).
  double polygonArea(const std::vector<double> & p)
  {
    const size_t n = p.size() / 2;
    double twice = 0.0;
    for (size_t i = 0; i < n; i++)
    {
      const size_t j = (i + 1) % n;
      twice += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
    }
    return std::fabs(twice) / 2.0;
  }

  QJsonArray rgbArray(const int c[3])
  {
    QJsonArray a;
    a.append(c[0]);
    a.append(c[1]);
    a.append(c[2]);
    return a;
  }

  QJsonArray pairArray(double a, double b)
  {
    QJsonArray p;
    p.append(a);
    p.append(b);
    return p;
  }

  // THE OUTDOOR LIGHT's columns: each name sent (the public names, dbd/LightData.dbd) and the database column it is read
  // from (an array's elements are numbered from 1 there, from 0 in the names sent).
  struct Column
  {
    const char * name;
    const char * sql;
  };
  const Column kColourColumns[] = {
    {"DirectColor", "DirectColor"}, {"AmbientColor", "AmbientColor"}, {"SkyTopColor", "SkyTopColor"},
    {"SkyMiddleColor", "SkyMiddleColor"}, {"SkyBand1Color", "SkyBand1Color"}, {"SkyBand2Color", "SkyBand2Color"},
    {"SkySmogColor", "SkySmogColor"}, {"SkyFogColor", "SkyFogColor"}, {"SunColor", "SunColor"},
    {"CloudSunColor", "CloudSunColor"}, {"CloudEmissiveColor", "CloudEmissiveColor"},
    {"CloudLayer1AmbientColor", "CloudLayer1AmbientColor"}, {"CloudLayer2AmbientColor", "CloudLayer2AmbientColor"},
    {"OceanCloseColor", "OceanCloseColor"}, {"OceanFarColor", "OceanFarColor"}, {"RiverCloseColor", "RiverCloseColor"},
    {"RiverFarColor", "RiverFarColor"}, {"ShadowOpacity", "ShadowOpacity"}, {"HorizonAmbientColor", "HorizonAmbientColor"},
    {"GroundAmbientColor", "GroundAmbientColor"}, {"EndFogColor", "EndFogColor"}, {"SunFogColor", "SunFogColor"},
    {"FogHeightColor", "FogHeightColor"}, {"EndFogHeightColor", "EndFogHeightColor"},
  };
  const Column kFloatColumns[] = {
    {"FogEnd", "FogEnd"}, {"FogScaler", "FogScaler"}, {"FogDensity", "FogDensity"}, {"FogHeight", "FogHeight"},
    {"FogHeightScaler", "FogHeightScaler"}, {"FogHeightDensity", "FogHeightDensity"}, {"FogZScalar", "FogZScalar"},
    {"MainFogStartDist", "MainFogStartDist"}, {"MainFogEndDist", "MainFogEndDist"}, {"SunFogAngle", "SunFogAngle"},
    {"CloudDensity", "CloudDensity"}, {"EndFogColorDistance", "EndFogColorDistance"}, {"FogStartOffset", "FogStartOffset"},
    {"SunFogStrength", "SunFogStrength"},
    {"FogHeightCoefficients0", "FogHeightCoefficients1"}, {"FogHeightCoefficients1", "FogHeightCoefficients2"},
    {"FogHeightCoefficients2", "FogHeightCoefficients3"}, {"FogHeightCoefficients3", "FogHeightCoefficients4"},
    {"MainFogCoefficients0", "MainFogCoefficients1"}, {"MainFogCoefficients1", "MainFogCoefficients2"},
    {"MainFogCoefficients2", "MainFogCoefficients3"}, {"MainFogCoefficients3", "MainFogCoefficients4"},
    {"HeightDensityFogCoeff0", "HeightDensityFogCoeff1"}, {"HeightDensityFogCoeff1", "HeightDensityFogCoeff2"},
    {"HeightDensityFogCoeff2", "HeightDensityFogCoeff3"}, {"HeightDensityFogCoeff3", "HeightDensityFogCoeff4"},
  };
  const Column kIntColumns[] = {
    {"ColorGradingFileDataID", "ColorGradingFileDataID"}, {"DarkerColorGradingFileDataID", "DarkerColorGradingFileDataID"},
  };
  const int kColours = (int)(sizeof(kColourColumns) / sizeof(kColourColumns[0]));
  const int kFloats = (int)(sizeof(kFloatColumns) / sizeof(kFloatColumns[0]));
  const int kInts = (int)(sizeof(kIntColumns) / sizeof(kIntColumns[0]));
  const char * const kMapTable = "Map";

  QStringList columnNames(const Column * columns, int n)
  {
    QStringList names;
    for (int k = 0; k < n; k++)
      names << QString::fromLatin1(columns[k].name);
    return names;
  }

  // The database column a name is read from, as database.xml declares it (an array by its own name).
  QString fieldOf(const Column & c)
  {
    QString f = QString::fromLatin1(c.sql);
    if (f.contains("Coeff"))   // the three arrays: FogHeightCoefficients1 -> FogHeightCoefficients
      f.chop(1);
    return f;
  }

  float floatOfBits(quint32 bits)
  {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
  }

  // A float as the player is sent it: the shortest decimal that reads back as the same float (at most 9 significant
  // digits), as a double -- QJsonDocument writes a double's shortest form, so the text sent is that decimal and the player
  // reads exactly the float the client stores. A value that is not finite goes as 0 (counted). Memoised by the bits.
  double floatForJson(float f, std::unordered_map<quint32, double> & memo, int & notFinite)
  {
    if (!std::isfinite(f))
    {
      notFinite++;
      return 0.0;
    }
    // (A whole number below 2^24 is its own shortest form. A negative zero compares equal to 0 and goes as 0, its sign
    // lost: none of the five audited clients' light floats is one (probed in their databases, 2026-10-21); a column whose
    // sign of zero mattered would need a case of its own.)
    if (f == 0.0f)
      return 0.0;
    if (std::fabs(f) < 16777216.0f && f == std::floor(f))
      return (double)f;
    quint32 bits;
    std::memcpy(&bits, &f, sizeof(bits));
    const auto kept = memo.find(bits);
    if (kept != memo.end())
      return kept->second;
    double d = (double)f;
    char text[40];
    for (int digits = 1; digits <= 9; digits++)
    {
      std::snprintf(text, sizeof(text), "%.*g", digits, (double)f);
      d = std::strtod(text, nullptr);
      if ((float)d == f)
        break;
    }
    memo[bits] = d;
    return d;
  }

}

QString wow::LightCatalog::statusName(Status s)
{
  switch (s)
  {
    case Status::NoClient: return "no client";
    case Status::Unavailable: return "unavailable";
    case Status::Ready: return "ready";
  }
  return "unknown";
}

std::shared_ptr<wow::LightCatalog> wow::LightCatalog::noClient(const QString & why)
{
  std::shared_ptr<LightCatalog> c(new LightCatalog());
  c->m_status = Status::NoClient;
  c->m_statusDetail = why;
  return c;
}

std::shared_ptr<wow::LightCatalog> wow::LightCatalog::build()
{
  QElapsedTimer total;
  total.start();
  std::shared_ptr<LightCatalog> c(new LightCatalog());
  const auto finish = [&c, &total](Status status, const QString & detail) {
    c->m_status = status;
    c->m_statusDetail = detail;
    c->m_buildMs = total.nsecsElapsed() / 1e6;
    if (status != Status::Ready)
      LOG_WARNING << "[lights] catalogue:" << c->m_product.toUtf8().constData() << c->m_build.toUtf8().constData()
                  << statusName(status).toUtf8().constData() << "--" << detail.toUtf8().constData();
    return c;
  };

  WoWFolder * folder = activeWowFolder();
  if (!core::Game::instance().initDone() || !GAMEDIRECTORY.clientProfile().isValid())
    return finish(Status::NoClient, "no client is loaded");
  const core::ClientProfile & profile = GAMEDIRECTORY.clientProfile();
  c->m_product = profile.product;
  c->m_build = GAMEDIRECTORY.version();
  c->m_dataPath = GAMEDIRECTORY.path();
  c->m_storageGeneration = folder ? folder->storageGeneration() : 0;
  if (!folder || profile.storage != core::StorageType::CASC)
    return finish(Status::Unavailable, profile.storage == core::StorageType::MPQ
                                         ? QString("a legacy MPQ client: its lights are in DBC tables this viewer does not read")
                                         : QString("the loaded client has no game storage this catalogue reads"));
  WoWDatabase * db = dynamic_cast<WoWDatabase *>(&GAMEDATABASE);
  // The database's schema check must be this client's: one that failed to open keeps none.
  if (!db || db->schemaCheck().build != c->m_build || db->schemaCheck().product != c->m_product)
    return finish(Status::Unavailable, "this client's database was not read");

  // How each table was read, and its rows here. All five must be read from a verified layout.
  const char * const names[] = {kLightTable, kParamsTable, kDataTable, kZoneTable, kPointTable};
  for (const char * name : names)
  {
    const WoWDatabase::SchemaCheck::TableReading * reading = db->tableReading(name);
    Table t;
    t.name = QString::fromLatin1(name);
    t.layout = reading ? reading->layout : QString();
    t.state = readingState(reading);
    t.read = readable(reading);
    t.rows = t.read ? countRows(name) : -1;
    t.lockedRows = reading ? reading->lockedRows : 0;
    t.absentFields = reading ? reading->absentFields : QStringList();
    c->m_tables.push_back(t);
  }
  for (const Table & t : c->m_tables)
    if (!t.read || t.rows < 0)
      return finish(Status::Unavailable, t.name + ".db2 is " + (t.read ? QString("not queryable") : t.state));

  // Every value is masked to its column's width, as the dbd files and database.xml give it: Time to 16 bits (a constant
  // bit 16 in 360DA016's pallet), the colours to 24 bits (signed 25-bit storage), the IDs to 16 bits.
  const sqlResult lights = GAMEDATABASE.sqlQuery(
    QString("SELECT ID, GameCoords1, GameCoords2, GameCoords3, GameFalloffStart, GameFalloffEnd, ContinentID, "
            "LightParamsID%1 & 65535 FROM %2").arg(SLOT + 1).arg(kLightTable));
  const sqlResult params = GAMEDATABASE.sqlQuery(
    QString("SELECT ID, WaterShallowAlpha, WaterDeepAlpha, OceanShallowAlpha, OceanDeepAlpha FROM %1").arg(kParamsTable));
  const sqlResult keys = GAMEDATABASE.sqlQuery(
    QString("SELECT LightParamID & 65535, Time & 65535, RiverCloseColor & 16777215, RiverFarColor & 16777215, "
            "OceanCloseColor & 16777215, OceanFarColor & 16777215, SkyBand1Color & 16777215 FROM %1 ORDER BY 1, 2, ID")
      .arg(kDataTable));
  const sqlResult zones = GAMEDATABASE.sqlQuery(
    QString("SELECT ID, MapID, LightID, Zmin, Zmax, PlayerConditionID FROM %1 ORDER BY ID").arg(kZoneTable));
  const sqlResult points = GAMEDATABASE.sqlQuery(
    QString("SELECT ZoneLightID, PointOrder & 255, Pos1, Pos2, ID FROM %1").arg(kPointTable));
  if (!lights.valid || !params.valid || !keys.valid || !zones.valid || !points.valid)
    return finish(Status::Unavailable, "the light tables could not be queried");
  const sqlResult masked = GAMEDATABASE.sqlQuery(
    QString("SELECT COUNT(*) FROM %1 WHERE ((RiverCloseColor | RiverFarColor | OceanCloseColor | OceanFarColor) & "
            "-16777216) != 0").arg(kDataTable));
  if (masked.valid && !masked.values.empty() && !masked.values[0].empty())
    c->m_counts.coloursMasked = masked.values[0][0].toLongLong();

  Counts & n = c->m_counts;
  for (const std::vector<QString> & r : lights.values)
  {
    if (r.size() < 8)
      continue;
    Light l;
    l.x = r[1].toDouble();
    l.y = r[2].toDouble();
    l.z = r[3].toDouble();
    l.start = r[4].toDouble();
    l.end = r[5].toDouble();
    l.map = r[6].toInt();
    l.params = r[7].toInt();
    c->m_lights[r[0].toInt()] = l;
  }
  for (const std::vector<QString> & r : params.values)
  {
    if (r.size() < 5)
      continue;
    Alphas a;
    a.waterShallow = r[1].toDouble();
    a.waterDeep = r[2].toDouble();
    a.oceanShallow = r[3].toDouble();
    a.oceanDeep = r[4].toDouble();
    for (double v : {a.waterShallow, a.waterDeep, a.oceanShallow, a.oceanDeep})
      if (!(v >= 0.0 && v <= 1.0))
      {
        n.alphasOutsideRange++;
        break;
      }
    c->m_alphas[r[0].toInt()] = a;
  }
  // The keys come sorted by LightParams, time and ID: a time repeated within a day keeps its first key by ID.
  for (const std::vector<QString> & r : keys.values)
  {
    if (r.size() < 7)
      continue;
    Key k;
    k.time = r[1].toInt();
    if (k.time < 0 || k.time >= DAY)
    {
      n.keysOutsideDay++;
      continue;
    }
    k.riverClose = (quint32)r[2].toLongLong();
    k.riverFar = (quint32)r[3].toLongLong();
    k.oceanClose = (quint32)r[4].toLongLong();
    k.oceanFar = (quint32)r[5].toLongLong();
    k.sky = (quint32)r[6].toLongLong();
    std::vector<Key> & day = c->m_keys[r[0].toInt()];
    if (!day.empty() && day.back().time == k.time)
    {
      n.keysRepeated++;
      continue;
    }
    day.push_back(k);
  }
  // The polygons: each zone's corners in PointOrder (then ID), (x, y) as stored.
  std::map<int, std::vector<std::tuple<int, int, double, double>>> corners;
  for (const std::vector<QString> & r : points.values)
  {
    if (r.size() < 5)
      continue;
    bool ok = false;
    const int zone = r[0].toInt(&ok);
    if (!ok)
    {
      n.pointsWithoutZone++;   // no relationship entry
      continue;
    }
    corners[zone].push_back(std::make_tuple(r[1].toInt(), r[4].toInt(), r[2].toDouble(), r[3].toDouble()));
  }
  std::set<int> zoneIds;
  for (const std::vector<QString> & r : zones.values)
  {
    if (r.size() < 6)
      continue;
    Zone z;
    z.id = r[0].toInt();
    z.map = r[1].toInt();
    z.light = r[2].toInt();
    z.zmin = r[3].toDouble();
    z.zmax = r[4].toDouble();
    z.conditional = r[5].toInt() != 0;
    zoneIds.insert(z.id);
    // Every ZoneLight row's light is a zone's, whatever its points: never sent as a point light (INFERRED).
    c->m_zoneLights.insert(z.light);
    if (c->m_lights.find(z.light) == c->m_lights.end())
      n.zonesWithoutLight++;
    auto own = corners.find(z.id);
    if (own == corners.end() || own->second.size() < 3)
    {
      n.zonesWithoutPoints++;
      continue;
    }
    std::vector<std::tuple<int, int, double, double>> & list = own->second;
    std::sort(list.begin(), list.end());
    for (size_t k = 0; k < list.size(); k++)
      if (std::get<0>(list[k]) != (int)k + 1)
      {
        n.polygonsOutOfOrder++;
        break;
      }
    z.poly.reserve(list.size() * 2);
    for (const auto & p : list)
    {
      z.poly.push_back(std::get<2>(p));
      z.poly.push_back(std::get<3>(p));
    }
    z.area = polygonArea(z.poly);
    c->m_zones.push_back(std::move(z));
  }
  for (const auto & g : corners)
    if (zoneIds.find(g.first) == zoneIds.end())
      n.pointsWithoutZone += (int)g.second.size();

  // LightParams' copy rows, for a slot-0 LightParams with no keys of its own.
  if (readCopyRows(QString::fromLatin1(kParamsTable), c->m_copySource))
    n.copyRows = (int)c->m_copySource.size();
  for (const auto & it : c->m_lights)
  {
    const int p = it.second.params;
    if (p == 0)
      n.lightsWithoutSlot++;
    else if (c->m_alphas.find(p) == c->m_alphas.end())
      n.lightsWithoutParams++;
    else if (c->m_keys.find(p) == c->m_keys.end())
    {
      const auto source = c->m_copySource.find(p);
      if (source != c->m_copySource.end() && c->m_keys.find(source->second) != c->m_keys.end())
        n.lightsByCopySource++;
      else
        n.lightsWithoutKeys++;
    }
  }

  finish(Status::Ready, QString());
  QStringList tableLines;
  for (const Table & t : c->m_tables)
    tableLines << QString("%1 %2 row(s) (layout %3, %4%5)").arg(t.name).arg(t.rows).arg(t.layout.isEmpty() ? QString("-") : t.layout)
                    .arg(t.state).arg(t.lockedRows > 0 ? QString(", %1 locked").arg(t.lockedRows) : QString());
  LOG_INFO << "[lights] catalogue:" << c->m_product.toUtf8().constData() << c->m_build.toUtf8().constData() << ":"
           << (unsigned int)c->m_lights.size() << "lights," << (unsigned int)c->m_alphas.size() << "LightParams,"
           << (unsigned int)c->m_keys.size() << "days of keys," << (unsigned int)c->m_zones.size() << "zone polygons in"
           << c->m_buildMs << "ms; slot 0: none on" << n.lightsWithoutSlot << "light(s), no LightParams row on"
           << n.lightsWithoutParams << ", no keys on" << n.lightsWithoutKeys << ", its copy source's keys on"
           << n.lightsByCopySource << "; keys outside the day" << n.keysOutsideDay << ", repeated" << n.keysRepeated
           << ", colours masked" << (int)n.coloursMasked << "; alphas outside [0, 1]" << n.alphasOutsideRange
           << "; zones without points" << n.zonesWithoutPoints << ", without a light" << n.zonesWithoutLight
           << ", out of order" << n.polygonsOutOfOrder << "; points without a zone" << n.pointsWithoutZone
           << "; LightParams copy rows" << n.copyRows << "; tables" << tableLines.join(", ").toUtf8().constData();
  return c;
}

bool wow::LightCatalog::belongsToActiveClient() const
{
  if (m_status == Status::NoClient)
    return false;
  WoWFolder * folder = activeWowFolder();
  return folder && GAMEDIRECTORY.clientProfile().isValid() && folder->storageGeneration() == m_storageGeneration &&
         GAMEDIRECTORY.clientProfile().product == m_product && GAMEDIRECTORY.version() == m_build &&
         GAMEDIRECTORY.path() == m_dataPath;
}

const wow::LightCatalog::Table * wow::LightCatalog::table(const QString & name) const
{
  for (const Table & t : m_tables)
    if (t.name == name)
      return &t;
  return nullptr;
}

bool wow::LightCatalog::sample(const std::vector<Key> & keys, int t, Water & out)
{
  if (keys.empty())
    return false;
  // The key at t; else the last key before t and the first after it, across midnight when t lies before the first key
  // or after the last (INFERRED: linear in time, as the keys' spacing suggests).
  const Key * a = nullptr;
  const Key * b = nullptr;
  double ta = 0.0, tb = 0.0;
  for (const Key & k : keys)
    if (k.time == t)
      a = b = &k;
  if (!a)
  {
    const Key * before = nullptr;
    const Key * after = nullptr;
    for (const Key & k : keys)
    {
      if (k.time < t)
        before = &k;
      else if (!after)
        after = &k;
    }
    a = before ? before : &keys.back();
    ta = before ? before->time : keys.back().time - DAY;
    b = after ? after : &keys.front();
    tb = after ? after->time : keys.front().time + DAY;
  }
  const double f = (a == b || tb <= ta) ? 0.0 : (t - ta) / (tb - ta);
  const quint32 from[5] = {a->riverClose, a->riverFar, a->oceanClose, a->oceanFar, a->sky};
  const quint32 to[5] = {b->riverClose, b->riverFar, b->oceanClose, b->oceanFar, b->sky};
  for (int k = 0; k < 5; k++)
    for (int j = 0; j < 3; j++)
    {
      const int shift = 16 - 8 * j;   // 0xRRGGBB
      const double va = (from[k] >> shift) & 0xFF;
      const double vb = (to[k] >> shift) & 0xFF;
      const int v = (int)std::floor(va + (vb - va) * f + 0.5);
      out.colour[k][j] = std::min(255, std::max(0, v));
    }
  return true;
}

QJsonObject wow::LightCatalog::unavailableJson(const QString & product, const QString & build, int mapId, const QString & why)
{
  QJsonObject o;
  o["state"] = "unavailable";
  o["detail"] = why;
  o["product"] = product;
  o["build"] = build;
  o["map"] = mapId;
  o["time"] = TIME;
  o["slot"] = SLOT;
  o["defaultLight"] = 0;
  o["defaultFrom"] = "none";
  o["zones"] = QJsonArray();
  o["points"] = QJsonArray();
  o["values"] = QJsonArray();
  return o;
}

QString wow::LightCatalog::describe(const QJsonObject & l)
{
  // (The detail appended, never passed through arg().)
  if (l.value("state").toString() != "ok")
    return "lights " + l.value("state").toString() + ": " + l.value("detail").toString();
  return QString("lights ok (map %1: %2 zones, %3 points, %4 values, default light %5 %6)")
    .arg(l.value("map").toInt()).arg(l.value("zones").toArray().size()).arg(l.value("points").toArray().size())
    .arg(l.value("values").toArray().size()).arg(l.value("defaultLight").toInt()).arg(l.value("defaultFrom").toString());
}

QJsonObject wow::LightCatalog::mapJson(int mapId) const
{
  if (m_status != Status::Ready)
    return unavailableJson(m_product, m_build, mapId, m_statusDetail);
  const auto kept = m_maps.find(mapId);
  if (kept != m_maps.end())
    return kept->second;
  const QJsonObject o = makeMapJson(mapId);
  m_maps[mapId] = o;
  return o;
}

wow::LightCatalog::Selection wow::LightCatalog::select(int mapId) const
{
  Selection s;
  // ZONES: the map's polygons in the order the player tries them (the first that holds the camera wins): those without a
  // player condition first, then the conditional ones; within each, the smallest area first, so a polygon nested in
  // another ("Durotar Org" in "Durotar") comes before it; ties by ID. INFERRED: which polygon the client picks is not
  // established. A zone's light is never filtered by its ContinentID (76 Retail polygons use another map's Light row).
  for (const Zone & z : m_zones)
    if (z.map == mapId)
      s.zones.push_back(&z);
  std::sort(s.zones.begin(), s.zones.end(), [](const Zone * a, const Zone * b) {
    if (a->conditional != b->conditional)
      return !a->conditional;
    if (a->area != b->area)
      return a->area < b->area;
    return a->id < b->id;
  });
  for (const Zone * z : s.zones)
    s.used.insert(z->light);
  // POINTS: the map's lights away from the origin that no ZoneLight row of the client names (a zone light's own position
  // is not used as a point light: UNKNOWN in the client), by ID. THE DEFAULT: the map's rows at the origin, one whose
  // falloff end is 0 first, else the lowest ID; a map with none takes Light 1 (INFERRED: the client's own default for such
  // a map is not established).
  int end0 = 0, origin = 0;
  bool haveEnd0 = false, haveOrigin = false;
  for (const auto & it : m_lights)
  {
    const Light & l = it.second;
    if (l.map != mapId)
      continue;
    if (l.x == 0.0 && l.y == 0.0 && l.z == 0.0)
    {
      if (l.end == 0.0 && !haveEnd0)
      {
        end0 = it.first;
        haveEnd0 = true;
      }
      if (!haveOrigin)
      {
        origin = it.first;
        haveOrigin = true;
      }
      continue;
    }
    if (m_zoneLights.find(it.first) != m_zoneLights.end())
      continue;
    s.points.push_back(it.first);
    s.used.insert(it.first);
  }
  if (haveEnd0)
  {
    s.defaultLight = end0;
    s.defaultFrom = "end0";
  }
  else if (haveOrigin)
  {
    s.defaultLight = origin;
    s.defaultFrom = "origin";
  }
  else if (m_lights.find(1) != m_lights.end())
  {
    s.defaultLight = 1;
    s.defaultFrom = "light1";
  }
  if (s.defaultLight != 0)
    s.used.insert(s.defaultLight);
  return s;
}

QJsonObject wow::LightCatalog::zoneJson(const Zone & z) const
{
  QJsonObject o;
  o["zone"] = z.id;
  o["light"] = z.light;
  o["zmin"] = z.zmin;
  o["zmax"] = z.zmax;
  o["conditional"] = z.conditional ? 1 : 0;
  QJsonArray poly;
  for (double v : z.poly)
    poly.append(v);
  o["poly"] = poly;
  return o;
}

QJsonObject wow::LightCatalog::pointJson(int id, const Light & l) const
{
  QJsonObject p;
  p["light"] = id;
  p["x"] = l.x;
  p["y"] = l.y;
  p["z"] = l.z;
  p["start"] = l.start;
  p["end"] = l.end;
  return p;
}

QJsonObject wow::LightCatalog::makeMapJson(int mapId) const
{
  const Selection s = select(mapId);
  const std::set<int> & used = s.used;
  QJsonArray zoneArray;
  for (const Zone * z : s.zones)
    zoneArray.append(zoneJson(*z));
  QJsonArray pointArray;
  for (int id : s.points)
    pointArray.append(pointJson(id, m_lights.at(id)));
  const int defaultLight = s.defaultLight;
  const QString defaultFrom = s.defaultFrom;
  // VALUES: each light's slot-0 LightParams at TIME -- its LightData keys, else its copy source's -- with the sky colour the
  // player gives a water reflection that finds no scene ("sky": SkyBand1Color, the column measured nearest the game's sky
  // above the horizon at one spot, dbd/LightData.dbd: MEASURED there, INFERRED elsewhere). A light whose four water colours
  // are all 0 there gets none, whatever its sky (Classic Beta's are 0 on most rows, among them every map default and zone
  // light of Eastern Kingdoms and Kalimdor; Retail's zone lights 7, 21 and 72: PROVEN_BYTES), nor does one without a
  // slot-0 LightParams, its row or keys: the player then draws the liquid catalogue's colours where it is the base light.
  QJsonArray valueArray;
  for (int id : used)
  {
    const auto light = m_lights.find(id);
    if (light == m_lights.end() || light->second.params == 0)
      continue;
    const int params = light->second.params;
    const auto alphas = m_alphas.find(params);
    if (alphas == m_alphas.end())
      continue;
    auto day = m_keys.find(params);
    if (day == m_keys.end())
    {
      const auto source = m_copySource.find(params);
      if (source != m_copySource.end())
        day = m_keys.find(source->second);
    }
    Water w;
    if (day == m_keys.end() || !sample(day->second, TIME, w))
      continue;
    bool coloured = false;
    for (int k = 0; k < 4; k++)
      for (int j = 0; j < 3; j++)
        coloured = coloured || w.colour[k][j] != 0;
    if (!coloured)
      continue;
    QJsonObject v;
    v["light"] = id;
    v["lightParams"] = params;
    v["riverClose"] = rgbArray(w.colour[0]);
    v["riverFar"] = rgbArray(w.colour[1]);
    v["oceanClose"] = rgbArray(w.colour[2]);
    v["oceanFar"] = rgbArray(w.colour[3]);
    v["sky"] = rgbArray(w.colour[SKY]);
    v["waterAlpha"] = pairArray(alphas->second.waterShallow, alphas->second.waterDeep);
    v["oceanAlpha"] = pairArray(alphas->second.oceanShallow, alphas->second.oceanDeep);
    valueArray.append(v);
  }
  QJsonObject o;
  o["state"] = "ok";
  o["detail"] = QString();
  o["product"] = m_product;
  o["build"] = m_build;
  o["map"] = mapId;
  o["time"] = TIME;
  o["slot"] = SLOT;
  o["defaultLight"] = defaultLight;
  o["defaultFrom"] = defaultFrom;
  o["zones"] = zoneArray;
  o["points"] = pointArray;
  o["values"] = valueArray;
  return o;
}

// ---------------------------------------------------------------- THE OUTDOOR LIGHT (Stage 10)

const QStringList & wow::LightCatalog::worldLightColourColumns()
{
  static const QStringList names = columnNames(kColourColumns, kColours);
  return names;
}

const QStringList & wow::LightCatalog::worldLightFloatColumns()
{
  static const QStringList names = columnNames(kFloatColumns, kFloats);
  return names;
}

const QStringList & wow::LightCatalog::worldLightIntColumns()
{
  static const QStringList names = columnNames(kIntColumns, kInts);
  return names;
}

QString wow::LightCatalog::branchOf(const QString & product, const QString & build)
{
  // PROVEN_BYTES for the five audited clients (docs/world-viewer/10-light-sky-fog.md 4.0: the core shader containers are
  // byte-identical in Retail 12.1.5, Classic Era 1.15.9 and MoP Classic 5.5.4, and another set in the PTR 12.1.7 and
  // Classic Beta 1.60.1); INFERRED for any other build, by its product and version.
  if (product == "wowt" || product == "wow_classic_beta")
    return "ptr";
  const QStringList v = build.split('.');
  const int major = v.value(0).toInt(), minor = v.value(1).toInt(), patch = v.value(2).toInt();
  if (major > 12 || (major == 12 && (minor > 1 || (minor == 1 && patch >= 7))) || (major == 1 && minor >= 60))
    return "ptr";
  return "retail";
}

void wow::LightCatalog::buildWorldLight()
{
  QElapsedTimer total;
  total.start();
  m_worldReady = false;
  m_worldCounts = WorldCounts();
  m_days.clear();
  m_params.clear();
  m_slots.clear();
  m_zonePriority.clear();
  m_mapLights.clear();
  m_absentColumns.clear();
  m_worldMaps.clear();
  m_worldMapMs.clear();
  if (m_status != Status::Ready)
  {
    // (build() has said why.)
    m_worldDetail = "the light catalogue is not ready (" + statusName(m_status) + ")" +
                    (m_statusDetail.isEmpty() ? QString() : ": " + m_statusDetail);
    m_worldBuildMs = total.nsecsElapsed() / 1e6;
    return;
  }
  const auto fail = [this, &total](const QString & why) {
    m_worldDetail = why;
    m_days.clear();
    m_worldBuildMs = total.nsecsElapsed() / 1e6;
    LOG_WARNING << "[lights] outdoor light:" << m_product.toUtf8().constData() << m_build.toUtf8().constData()
                << "unavailable --" << why.toUtf8().constData();
  };
  WorldCounts & n = m_worldCounts;
  const Table * data = table(QString::fromLatin1(kDataTable));
  m_lightDataLayout = data ? data->layout : QString();

  // The named columns this client's layout lacks (each read as 0: database.xml, dbd/LightData.dbd).
  const QStringList absent = data ? data->absentFields : QStringList();
  const auto noteAbsent = [this, &absent](const Column * columns, int count) {
    for (int k = 0; k < count; k++)
      if (absent.contains(fieldOf(columns[k])))
        m_absentColumns << QString::fromLatin1(columns[k].name);
  };
  noteAbsent(kColourColumns, kColours);
  noteAbsent(kFloatColumns, kFloats);
  noteAbsent(kIntColumns, kInts);

  // Every key's LightParams, time, ID and named columns, read as integers (no text: 55 columns of some 20,000 keys), in
  // the table's order (sorted below: the database's own sort of such rows costs more). The colours masked to 24 bits as
  // build() masks them; the floats are their 32-bit patterns (database.xml declares them uint32), read back below as the
  // floats stored.
  QElapsedTimer step;
  step.start();
  QStringList exprs, colours;
  exprs << "LightParamID & 65535"
        << "Time & 65535"
        << "ID";
  for (int k = 0; k < kColours; k++)
  {
    exprs << QString("%1 & 16777215").arg(kColourColumns[k].sql);
    colours << QString::fromLatin1(kColourColumns[k].sql);
  }
  for (int k = 0; k < kFloats; k++)
    exprs << QString::fromLatin1(kFloatColumns[k].sql);
  for (int k = 0; k < kInts; k++)
    exprs << QString::fromLatin1(kIntColumns[k].sql);
  const int columns = (int)exprs.size();
  std::vector<quint32> values;
  // (Room for every row the table holds: build() counted them.)
  values.reserve((size_t)columns * (size_t)std::max<qint64>(data ? data->rows : 0, 0));
  bool shape = true;
  const bool keysRead = GAMEDATABASE.sqlQueryIntegers(QString("SELECT %1 FROM %2").arg(exprs.join(", ")).arg(kDataTable),
                                                      [&values, &shape, columns](const qint64 * v, int count) {
                                                        shape = shape && count == columns;
                                                        for (int k = 0; k < count && k < columns; k++)
                                                          values.push_back((quint32)v[k]);
                                                      });
  QStringList slotColumns;
  for (int k = 1; k <= SLOTS; k++)
    slotColumns << QString("LightParamsID%1 & 65535").arg(k);
  const sqlResult slotRows = GAMEDATABASE.sqlQuery(QString("SELECT ID, %1 FROM %2").arg(slotColumns.join(", ")).arg(kLightTable));
  const sqlResult params = GAMEDATABASE.sqlQuery(
    QString("SELECT ID, Glow, LightSkyboxID, Flags, HighlightSky, OverrideSunPosition1, OverrideSunPosition2, "
            "OverrideSunPosition3, SunPolar, SunAzimuth FROM %1").arg(kParamsTable));
  const sqlResult zones = GAMEDATABASE.sqlQuery(QString("SELECT ID, TransitionType FROM %1").arg(kZoneTable));
  // (Map is the world map catalogue's table too; a client whose Map could not be read gives no row: every map then has
  // no override and no flags.)
  const sqlResult maps = GAMEDATABASE.sqlQuery(QString("SELECT ID, TimeOfDayOverride, Flags1, Flags2, Flags3 FROM %1")
                                                 .arg(kMapTable));
  if (!keysRead || !shape || !slotRows.valid || !params.valid || !zones.valid || !maps.valid)
    return fail("the outdoor light's columns could not be queried");
  const sqlResult masked = GAMEDATABASE.sqlQuery(QString("SELECT COUNT(*) FROM %1 WHERE ((%2) & -16777216) != 0")
                                                   .arg(kDataTable).arg(colours.join(" | ")));
  if (masked.valid && !masked.values.empty() && !masked.values[0].empty())
    n.coloursMasked = masked.values[0][0].toLongLong();

  // The keys, sorted by LightParams, time and ID (build()'s order): a time repeated within a day keeps its first key by
  // ID (build()'s rule).
  std::vector<size_t> order(values.size() / columns);
  for (size_t k = 0; k < order.size(); k++)
    order[k] = k;
  std::sort(order.begin(), order.end(), [&values, columns](size_t a, size_t b) {
    const quint32 * x = &values[a * columns];
    const quint32 * y = &values[b * columns];
    return std::make_tuple(x[0], x[1], x[2]) < std::make_tuple(y[0], y[1], y[2]);
  });
  for (size_t k : order)
  {
    const quint32 * v = &values[k * columns];
    const int time = (int)v[1];
    if (time < 0 || time >= DAY)
    {
      n.keysOutsideDay++;
      continue;
    }
    Day & d = m_days[(int)v[0]];
    if (!d.times.empty() && d.times.back() == time)
    {
      n.keysRepeated++;
      continue;
    }
    const quint32 * named = v + 3;
    d.times.push_back(time);
    d.colours.insert(d.colours.end(), named, named + kColours);
    for (int j = 0; j < kFloats; j++)
      d.floats.push_back(floatOfBits(named[kColours + j]));
    d.ints.insert(d.ints.end(), named + kColours + kFloats, named + kColours + kFloats + kInts);
    n.keys++;
  }
  n.days = (int)m_days.size();
  for (const std::vector<QString> & r : slotRows.values)
  {
    if (r.size() < 1 + SLOTS)
      continue;
    std::vector<int> s(SLOTS);
    for (int k = 0; k < SLOTS; k++)
    {
      s[k] = r[1 + k].toInt();
      if (s[k] != 0)
        n.lightsWithSlot[k]++;
    }
    m_slots[r[0].toInt()] = s;
  }
  for (const std::vector<QString> & r : params.values)
  {
    if (r.size() < 10)
      continue;
    Params p;
    p.glow = floatOfBits((quint32)r[1].toLongLong());
    p.skybox = r[2].toInt();
    p.flags = (quint32)r[3].toLongLong();
    p.highlightSky = r[4].toInt();
    for (int k = 0; k < 3; k++)
      p.overrideSun[k] = floatOfBits((quint32)r[5 + k].toLongLong());
    p.sunPolar = floatOfBits((quint32)r[8].toLongLong());
    p.sunAzimuth = floatOfBits((quint32)r[9].toLongLong());
    m_params[r[0].toInt()] = p;
  }
  n.params = (int)m_params.size();
  for (const std::vector<QString> & r : zones.values)
  {
    if (r.size() < 2)
      continue;
    const int priority = r[1].toInt();
    m_zonePriority[r[0].toInt()] = priority;
    n.zonesByPriority[priority]++;
  }
  for (const std::vector<QString> & r : maps.values)
  {
    if (r.size() < 5)
      continue;
    MapLight m;
    m.timeOverride = r[1].toInt();
    for (int k = 0; k < 3; k++)
      m.flags[k] = (quint32)r[2 + k].toLongLong();
    m_mapLights[r[0].toInt()] = m;
    n.maps++;
    if (m.timeOverride != -1)
    {
      n.mapsOverridden++;
      n.overrideMax = std::max(n.overrideMax, m.timeOverride);
    }
    if (m.flags[2] & 0x2)
      n.mapsFlag2Bit1++;
  }
  // The lights whose slot-0 / slot-1 LightParams has no keys of its own nor its copy source's.
  for (const auto & it : m_slots)
    for (int k = 0; k < DAY_SLOTS; k++)
    {
      const int p = it.second[k];
      if (p == 0 || m_days.count(p))
        continue;
      const auto source = m_copySource.find(p);
      if (source == m_copySource.end() || !m_days.count(source->second))
        n.lightsWithoutDay[k]++;
    }
  n.readMs = step.nsecsElapsed() / 1e6;

  // THE FRAGMENTS: each day as it is sent, made once here so that a map's object is only put together from them.
  step.restart();
  std::unordered_map<quint32, double> memo;
  for (auto & it : m_days)
  {
    Day & d = it.second;
    QJsonArray times, c, f, i;
    for (int t : d.times)
      times.append(t);
    for (quint32 v : d.colours)
      c.append((double)v);
    for (float v : d.floats)
      f.append(floatForJson(v, memo, n.floatsNotFinite));
    for (quint32 v : d.ints)
      i.append((double)v);
    QJsonObject o;
    o["times"] = times;
    o["c"] = c;
    o["f"] = f;
    o["i"] = i;
    d.fragment = o;
    // (Only the fragment is read from here on: the day's vectors released, some 4 MB on Retail.)
    std::vector<int>().swap(d.times);
    std::vector<quint32>().swap(d.colours);
    std::vector<float>().swap(d.floats);
    std::vector<quint32>().swap(d.ints);
  }
  n.fragmentMs = step.nsecsElapsed() / 1e6;

  m_worldReady = true;
  m_worldDetail.clear();
  m_worldBuildMs = total.nsecsElapsed() / 1e6;
  QStringList priorities;
  for (const auto & p : n.zonesByPriority)
    priorities << QString("%1: %2").arg(p.first).arg(p.second);
  LOG_INFO << "[lights] outdoor light:" << m_product.toUtf8().constData() << m_build.toUtf8().constData() << ":"
           << n.days << "days of" << (int)n.keys << "keys (LightData layout" << m_lightDataLayout.toUtf8().constData()
           << ", absent columns" << (m_absentColumns.isEmpty() ? QString("none") : m_absentColumns.join(" ")).toUtf8().constData()
           << "); keys outside the day" << n.keysOutsideDay << ", repeated" << n.keysRepeated << ", colours masked"
           << (int)n.coloursMasked << ", floats not finite" << n.floatsNotFinite << ";" << (int)m_slots.size()
           << "lights, slots set" << n.lightsWithSlot[0] << n.lightsWithSlot[1] << n.lightsWithSlot[2] << n.lightsWithSlot[3]
           << n.lightsWithSlot[4] << n.lightsWithSlot[5] << n.lightsWithSlot[6] << n.lightsWithSlot[7]
           << ", no day for slot 0 on" << n.lightsWithoutDay[0] << "and slot 1 on" << n.lightsWithoutDay[1] << ";" << n.params
           << "LightParams; zones by TransitionType" << priorities.join(", ").toUtf8().constData() << ";" << n.maps
           << "maps," << n.mapsOverridden << "with a time override (largest" << n.overrideMax << ")," << n.mapsFlag2Bit1
           << "with flag word 2 bit 0x2; read in" << n.readMs << "ms, fragments in" << n.fragmentMs << "ms, total"
           << m_worldBuildMs << "ms";
}

QJsonObject wow::LightCatalog::worldLightJson(int mapId) const
{
  if (m_status != Status::Ready || !m_worldReady)
    return worldLightUnavailableJson(m_product, m_build, mapId, m_status != Status::Ready ? m_statusDetail : m_worldDetail);
  const auto kept = m_worldMaps.find(mapId);
  if (kept != m_worldMaps.end())
    return kept->second;
  QElapsedTimer t;
  t.start();
  const QJsonObject o = makeWorldLightJson(mapId);
  const double ms = t.nsecsElapsed() / 1e6;
  m_worldMaps[mapId] = o;
  m_worldMapMs[mapId] = ms;
  LOG_INFO << "[lights]" << describeWorldLight(o).toUtf8().constData() << "assembled in" << ms << "ms";
  return o;
}

double wow::LightCatalog::worldLightAssemblyMilliseconds(int mapId) const
{
  const auto kept = m_worldMapMs.find(mapId);
  return kept != m_worldMapMs.end() ? kept->second : -1.0;
}

QJsonObject wow::LightCatalog::assembleWorldLightJson(int mapId, double * ms) const
{
  if (ms)
    *ms = -1.0;
  if (m_status != Status::Ready || !m_worldReady)
    return worldLightUnavailableJson(m_product, m_build, mapId, m_status != Status::Ready ? m_statusDetail : m_worldDetail);
  QElapsedTimer t;
  t.start();
  const QJsonObject o = makeWorldLightJson(mapId);
  if (ms)
    *ms = t.nsecsElapsed() / 1e6;
  return o;
}

QJsonObject wow::LightCatalog::makeWorldLightJson(int mapId) const
{
  // ZONES, POINTS and THE DEFAULT: Stage 09's (select(): its order, geometry and rules), each zone with its TransitionType
  // ("priority": the public name's reading, INFERRED; 0 where the layout has none).
  const Selection s = select(mapId);
  QJsonArray zoneArray;
  for (const Zone * z : s.zones)
  {
    QJsonObject o = zoneJson(*z);
    const auto priority = m_zonePriority.find(z->id);
    o["priority"] = priority != m_zonePriority.end() ? priority->second : 0;
    zoneArray.append(o);
  }
  QJsonArray pointArray;
  for (int id : s.points)
    pointArray.append(pointJson(id, m_lights.at(id)));
  // LIGHTS: every light those name, with its eight LightParams as the Light row stores them (copy rows by their own ID,
  // which the database keeps as rows of their own; 0: none). A light without a Light row (one in a section this computer
  // cannot read) has none.
  QJsonArray lightArray;
  std::set<int> paramIds;
  for (int id : s.used)
  {
    const auto row = m_slots.find(id);
    QJsonArray slotIds;
    for (int k = 0; k < SLOTS; k++)
    {
      const int p = row != m_slots.end() ? row->second[k] : 0;
      slotIds.append(p);
      if (k < DAY_SLOTS && p != 0)
        paramIds.insert(p);
    }
    QJsonObject l;
    l["light"] = id;
    l["slots"] = slotIds;
    lightArray.append(l);
  }
  // PARAMS and DAYS: each LightParams of slots 0 and 1 that has a row, by ID, with its day -- its own keys, else its copy
  // source's (Stage 09's rule), else none (-1) -- each day sent once, in the order first named.
  std::map<int, int> dayIndex;
  QJsonArray paramArray, dayArray;
  std::unordered_map<quint32, double> memo;
  int notFinite = 0;
  for (int id : paramIds)
  {
    const auto row = m_params.find(id);
    if (row == m_params.end())
      continue;
    auto day = m_days.find(id);
    if (day == m_days.end())
    {
      const auto source = m_copySource.find(id);
      if (source != m_copySource.end())
        day = m_days.find(source->second);
    }
    int index = -1;
    if (day != m_days.end())
    {
      const auto known = dayIndex.find(day->first);
      if (known != dayIndex.end())
        index = known->second;
      else
      {
        index = dayArray.size();
        dayIndex[day->first] = index;
        dayArray.append(day->second.fragment);
      }
    }
    const Params & p = row->second;
    QJsonObject o;
    o["id"] = id;
    o["day"] = index;
    o["glow"] = floatForJson(p.glow, memo, notFinite);
    o["skybox"] = p.skybox;
    o["flags"] = (double)p.flags;
    o["highlightSky"] = p.highlightSky;
    QJsonArray sun;
    for (int k = 0; k < 3; k++)
      sun.append(floatForJson(p.overrideSun[k], memo, notFinite));
    o["overrideSun"] = sun;
    o["sunPolar"] = floatForJson(p.sunPolar, memo, notFinite);
    o["sunAzimuth"] = floatForJson(p.sunAzimuth, memo, notFinite);
    paramArray.append(o);
  }
  // THE MAP: its time override (-1 none; the unit, minutes, INFERRED) and its three flag words, as read.
  const auto map = m_mapLights.find(mapId);
  QJsonArray mapFlags;
  for (int k = 0; k < 3; k++)
    mapFlags.append(map != m_mapLights.end() ? (double)map->second.flags[k] : 0.0);
  QJsonObject o;
  o["state"] = "ok";
  o["detail"] = QString();
  o["product"] = m_product;
  o["build"] = m_build;
  o["map"] = mapId;
  o["branch"] = branchOf(m_product, m_build);
  o["lightDataLayout"] = m_lightDataLayout;
  o["timeOverride"] = map != m_mapLights.end() ? map->second.timeOverride : -1;
  o["mapFlags"] = mapFlags;
  o["defaultLight"] = s.defaultLight;
  o["defaultFrom"] = s.defaultFrom;
  o["zones"] = zoneArray;
  o["points"] = pointArray;
  o["lights"] = lightArray;
  o["params"] = paramArray;
  o["colorColumns"] = QJsonArray::fromStringList(worldLightColourColumns());
  o["floatColumns"] = QJsonArray::fromStringList(worldLightFloatColumns());
  o["intColumns"] = QJsonArray::fromStringList(worldLightIntColumns());
  o["absentColumns"] = QJsonArray::fromStringList(m_absentColumns);
  o["days"] = dayArray;
  return o;
}

QJsonObject wow::LightCatalog::worldLightUnavailableJson(const QString & product, const QString & build, int mapId,
                                                         const QString & why)
{
  QJsonObject o;
  o["state"] = "unavailable";
  o["detail"] = why;
  o["product"] = product;
  o["build"] = build;
  o["map"] = mapId;
  o["branch"] = branchOf(product, build);
  o["lightDataLayout"] = QString();
  o["timeOverride"] = -1;
  QJsonArray mapFlags;
  for (int k = 0; k < 3; k++)
    mapFlags.append(0);
  o["mapFlags"] = mapFlags;
  o["defaultLight"] = 0;
  o["defaultFrom"] = "none";
  o["zones"] = QJsonArray();
  o["points"] = QJsonArray();
  o["lights"] = QJsonArray();
  o["params"] = QJsonArray();
  o["colorColumns"] = QJsonArray::fromStringList(worldLightColourColumns());
  o["floatColumns"] = QJsonArray::fromStringList(worldLightFloatColumns());
  o["intColumns"] = QJsonArray::fromStringList(worldLightIntColumns());
  o["absentColumns"] = QJsonArray();
  o["days"] = QJsonArray();
  return o;
}

QString wow::LightCatalog::describeWorldLight(const QJsonObject & w)
{
  // (The detail appended, never passed through arg().)
  if (w.value("state").toString() != "ok")
    return "outdoor light " + w.value("state").toString() + ": " + w.value("detail").toString();
  const QJsonArray days = w.value("days").toArray();
  int keys = 0;
  for (const QJsonValue & d : days)
    keys += d.toObject().value("times").toArray().size();
  return QString("outdoor light ok (map %1, branch %2: %3 zones, %4 points, %5 lights, %6 LightParams, %7 days of %8 keys, "
                 "default light %9 %10, time override %11)")
    .arg(w.value("map").toInt()).arg(w.value("branch").toString()).arg(w.value("zones").toArray().size())
    .arg(w.value("points").toArray().size()).arg(w.value("lights").toArray().size()).arg(w.value("params").toArray().size())
    .arg(days.size()).arg(keys).arg(w.value("defaultLight").toInt()).arg(w.value("defaultFrom").toString())
    .arg(w.value("timeOverride").toInt());
}
