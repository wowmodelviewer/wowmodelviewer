/*
 * LightCatalog.cpp
 *
 * See LightCatalog.h. The evidence for each column is in the dbd files (dbd/Light.dbd, LightParams.dbd, LightData.dbd,
 * ZoneLight.dbd, ZoneLightPoint.dbd) and docs/world-viewer/09-liquids.md.
 */

#include "LightCatalog.h"

#include <algorithm>
#include <cmath>
#include <tuple>

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

QJsonObject wow::LightCatalog::makeMapJson(int mapId) const
{
  std::set<int> used;
  // ZONES: the map's polygons in the order the player tries them (the first that holds the camera wins): those without a
  // player condition first, then the conditional ones; within each, the smallest area first, so a polygon nested in
  // another ("Durotar Org" in "Durotar") comes before it; ties by ID. INFERRED: which polygon the client picks is not
  // established. A zone's light is never filtered by its ContinentID (76 Retail polygons use another map's Light row).
  std::vector<const Zone *> order;
  for (const Zone & z : m_zones)
    if (z.map == mapId)
      order.push_back(&z);
  std::sort(order.begin(), order.end(), [](const Zone * a, const Zone * b) {
    if (a->conditional != b->conditional)
      return !a->conditional;
    if (a->area != b->area)
      return a->area < b->area;
    return a->id < b->id;
  });
  QJsonArray zoneArray;
  for (const Zone * z : order)
  {
    QJsonObject o;
    o["zone"] = z->id;
    o["light"] = z->light;
    o["zmin"] = z->zmin;
    o["zmax"] = z->zmax;
    o["conditional"] = z->conditional ? 1 : 0;
    QJsonArray poly;
    for (double v : z->poly)
      poly.append(v);
    o["poly"] = poly;
    zoneArray.append(o);
    used.insert(z->light);
  }
  // POINTS: the map's lights away from the origin that no ZoneLight row of the client names (a zone light's own position
  // is not used as a point light: UNKNOWN in the client), by ID. THE DEFAULT: the map's rows at the origin, one whose
  // falloff end is 0 first, else the lowest ID; a map with none takes Light 1 (INFERRED: the client's own default for such
  // a map is not established).
  QJsonArray pointArray;
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
    QJsonObject p;
    p["light"] = it.first;
    p["x"] = l.x;
    p["y"] = l.y;
    p["z"] = l.z;
    p["start"] = l.start;
    p["end"] = l.end;
    pointArray.append(p);
    used.insert(it.first);
  }
  int defaultLight = 0;
  QString defaultFrom = "none";
  if (haveEnd0)
  {
    defaultLight = end0;
    defaultFrom = "end0";
  }
  else if (haveOrigin)
  {
    defaultLight = origin;
    defaultFrom = "origin";
  }
  else if (m_lights.find(1) != m_lights.end())
  {
    defaultLight = 1;
    defaultFrom = "light1";
  }
  if (defaultLight != 0)
    used.insert(defaultLight);
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
