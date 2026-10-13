/*
 * WorldMapCatalog.cpp
 *
 * See WorldMapCatalog.h. The evidence for each column and rule is in docs/world-viewer/02-map-catalog.md.
 */

#include "WorldMapCatalog.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>

#include "Game.h"
#include "GameDatabase.h"
#include "logger/Logger.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"

namespace
{
  // Map.db2's columns as database.xml names them. Only these are read; every other column of the client's layout is
  // placed by Map.dbd but never selected.
  const char * const kTable = "Map";
  const char * const kWdtFileDataIdColumn = "WdtFileDataID";
  const char * const kMapTypeColumn = "MapType";
  const char * const kInstanceTypeColumn = "InstanceType";
  const char * const kParentMapColumn = "ParentMapID";
  const char * const kWorldCopyColumn = "WorldCopyOfMapID";
  const char * const kLocationMapColumn = "LocationMapID";

  wow::WoWFolder * activeWowFolder()
  {
    if (!core::Game::instance().initDone())
      return nullptr;
    return dynamic_cast<wow::WoWFolder *>(&GAMEDIRECTORY);
  }

  bool hasColumn(const QStringList & absent, const char * column)
  {
    return !absent.contains(QString::fromLatin1(column), Qt::CaseInsensitive);
  }

  wow::MapCode code(bool present, const QString & stored)
  {
    wow::MapCode c;
    c.present = present;
    c.value = present ? stored.toInt() : 0;
    return c;
  }

  QString qs(qint64 n) { return QString::number(n); }
}

QString wow::WorldMapCatalog::availabilityName(WdtAvailability a)
{
  switch (a)
  {
    case WdtAvailability::NotChecked: return "not checked";
    case WdtAvailability::Available: return "available";
    case WdtAvailability::NotInClientStorage: return "not in client storage";
    case WdtAvailability::NotInstalledLocally: return "not installed locally";
    case WdtAvailability::Unreadable: return "unreadable";
    case WdtAvailability::Unsupported: return "unsupported";
  }
  return "unknown";
}

QString wow::WorldMapCatalog::statusName(Status s)
{
  switch (s)
  {
    case Status::NoClient: return "no client";
    case Status::Unsupported: return "unsupported";
    case Status::MapTableUnavailable: return "Map.db2 unavailable";
    case Status::Ready: return "ready";
  }
  return "unknown";
}

std::shared_ptr<wow::WorldMapCatalog> wow::WorldMapCatalog::build()
{
  QElapsedTimer total;
  total.start();
  std::shared_ptr<WorldMapCatalog> c(new WorldMapCatalog());

  WoWFolder * folder = activeWowFolder();
  // A loaded client has a profile (setConfig, or the legacy loader): the folder and database objects alone exist
  // before any load too (-dbfromfile).
  if (!core::Game::instance().initDone() || !GAMEDIRECTORY.clientProfile().isValid())
  {
    c->m_status = Status::NoClient;
    c->m_statusDetail = "no client is loaded";
    return c;
  }

  const core::ClientProfile & profile = GAMEDIRECTORY.clientProfile();
  c->m_client.product = profile.product;
  c->m_client.build = GAMEDIRECTORY.version();
  c->m_client.locale = GAMEDIRECTORY.locale();
  c->m_client.dataPath = GAMEDIRECTORY.path();
  c->m_client.schema = core::Game::instance().configFolder();
  c->m_client.storageGeneration = folder ? folder->storageGeneration() : 0;
  c->m_client.rootNames = folder ? (qint64)folder->storageRootNameCount() : 0;
  c->m_client.rootNameless = folder ? (qint64)folder->storageRootNamelessCount() : 0;

  if (!folder || profile.storage != core::StorageType::CASC)
  {
    c->m_status = Status::Unsupported;
    c->m_statusDetail = profile.storage == core::StorageType::MPQ
                          ? QString("a legacy MPQ client: its maps are in Map.dbc, which this viewer does not read")
                          : QString("the loaded client has no game storage this catalogue reads");
    return c;
  }
  WoWDatabase * db = dynamic_cast<WoWDatabase *>(&GAMEDATABASE);
  // The database's schema check must be this client's: one that failed to open keeps none (clientDatabaseStarting).
  if (!db || db->schemaCheck().build != c->m_client.build || db->schemaCheck().product != c->m_client.product)
  {
    c->m_status = Status::MapTableUnavailable;
    c->m_statusDetail = "this client's database was not read";
    return c;
  }
  const WoWDatabase::SchemaCheck::TableReading * reading = db->tableReading(kTable);
  if (!reading)
  {
    c->m_status = Status::MapTableUnavailable;
    c->m_statusDetail = QString("the schema %1 has no Map table").arg(c->m_client.schema);
    return c;
  }

  c->m_layout = reading->layout;
  c->m_absentColumns = reading->absentFields;
  c->m_rowsInTable = reading->rows;
  c->m_lockedRows = reading->lockedRows;
  using State = WoWDatabase::SchemaCheck::TableReading::State;
  switch (reading->state)
  {
    case State::Verified:
      c->m_layoutState = "verified by its definition";
      break;
    case State::VerifiedBySchemaLayout:
      c->m_layoutState = "verified by the schema's layout";
      break;
    case State::KeptAtSchemaPositions:
      // database.xml reads Map only from a verified layout (verifiedLayoutOnly), so this cannot come from the schema
      // check; a hand-edited schema could: positions nothing verified are not used.
      c->m_status = Status::MapTableUnavailable;
      c->m_statusDetail = QString("Map.db2 layout %1 is not verified for build %2").arg(reading->layout, c->m_client.build);
      return c;
    case State::NotRead:
      c->m_status = Status::MapTableUnavailable;
      c->m_statusDetail = QString("Map.db2 does not match a known layout: %1").arg(reading->reason);
      return c;
    case State::NotInstalled:
      c->m_status = Status::MapTableUnavailable;
      c->m_statusDetail = "Map.db2 is listed by this client but not on this computer yet";
      return c;
    case State::NotInClient:
      c->m_status = Status::MapTableUnavailable;
      c->m_statusDetail = "this client has no Map.db2";
      return c;
  }

  // The client's own layout decides how its maps name their WDTs: a layout with the WdtFileDataID column names them
  // by FileDataID (Retail, Classic Beta); one without (Classic Era, MoP Classic) has none, and the directory rule
  // applies. Nothing about the product is assumed.
  const bool fdidColumn = hasColumn(c->m_absentColumns, kWdtFileDataIdColumn);
  c->m_rule = fdidColumn ? WdtRule::ByFileDataId : WdtRule::ByDirectoryPath;
  const bool mapTypeColumn = hasColumn(c->m_absentColumns, kMapTypeColumn);
  const bool instanceTypeColumn = hasColumn(c->m_absentColumns, kInstanceTypeColumn);
  const bool parentColumn = hasColumn(c->m_absentColumns, kParentMapColumn);
  const bool copyColumn = hasColumn(c->m_absentColumns, kWorldCopyColumn);
  const bool locationColumn = hasColumn(c->m_absentColumns, kLocationMapColumn);

  QElapsedTimer query;
  query.start();
  const sqlResult rows = GAMEDATABASE.sqlQuery(
    QString("SELECT ID, Directory, MapName_lang, %1, %2, %3, %4, %5, %6 FROM %7 ORDER BY ID")
      .arg(kWdtFileDataIdColumn, kMapTypeColumn, kInstanceTypeColumn, kParentMapColumn, kWorldCopyColumn,
           kLocationMapColumn, kTable));
  c->m_queryMs = query.nsecsElapsed() / 1e6;
  if (!rows.valid)
  {
    c->m_status = Status::MapTableUnavailable;
    c->m_statusDetail = "Map.db2's table could not be queried";
    return c;
  }

  c->m_maps.reserve(rows.values.size());
  for (const std::vector<QString> & row : rows.values)
  {
    if (row.size() < 9)
      continue;
    MapEntry e;
    e.id = row[0].toInt();
    e.directory = row[1];
    e.name = row[2];
    e.mapType = code(mapTypeColumn, row[4]);
    e.instanceType = code(instanceTypeColumn, row[5]);
    e.parentMapId = code(parentColumn, row[6]);
    e.worldCopyOfMapId = code(copyColumn, row[7]);
    e.locationMapId = code(locationColumn, row[8]);

    if (c->m_rule == WdtRule::ByFileDataId)
    {
      bool numeric = false;
      const int fdid = row[3].toInt(&numeric);
      if (numeric && fdid > 0)
      {
        e.wdt.kind = WdtReference::Kind::FileDataId;
        e.wdt.fileDataId = fdid;
      }
      else
        e.wdt.why = QString("Map.db2 names no WDT for this map (WdtFileDataID %1)").arg(row[3]);
    }
    else if (!e.directory.isEmpty())
    {
      // The rule the audit verified for these clients (docs/world-viewer/00-audit.md 4.1): the storage opens the WDT
      // by this path, and the bytes equal those opened by FileDataID for every map.
      e.wdt.kind = WdtReference::Kind::Path;
      e.wdt.path = QString("world/maps/%1/%1.wdt").arg(e.directory);
    }
    else
      e.wdt.why = "the map has no directory, so the client's directory rule names no WDT";
    if (e.wdt.kind == WdtReference::Kind::Unresolved)
    {
      e.inspection.state = WdtAvailability::Unsupported;
      e.inspection.detail = e.wdt.why;
    }
    c->m_maps.push_back(std::move(e));
  }
  c->m_status = Status::Ready;
  if (c->m_lockedRows > 0)
    c->m_statusDetail = QString("%1 Map.db2 record(s) are in sections encrypted with a key this computer does not have:"
                                " they cannot be listed").arg(c->m_lockedRows);
  c->m_buildMs = total.nsecsElapsed() / 1e6;

  size_t byFdid = 0, byPath = 0, unresolved = 0;
  for (const MapEntry & e : c->m_maps)
  {
    if (e.wdt.kind == WdtReference::Kind::FileDataId)
      byFdid++;
    else if (e.wdt.kind == WdtReference::Kind::Path)
      byPath++;
    else
      unresolved++;
  }
  LOG_INFO << "[mapcatalog]" << c->m_client.product.toUtf8().constData() << c->m_client.build.toUtf8().constData()
           << ":" << (unsigned int)c->m_maps.size() << "maps from Map.db2 (layout" << c->m_layout.toUtf8().constData()
           << c->m_layoutState.toUtf8().constData() << ");" << "WDT by FileDataID" << (unsigned int)byFdid
           << ", by directory path" << (unsigned int)byPath << ", unresolved" << (unsigned int)unresolved
           << "; encrypted records" << (int)c->m_lockedRows << "; query" << c->m_queryMs << "ms, build" << c->m_buildMs
           << "ms, ~" << (unsigned int)(c->approximateBytes() / 1024) << "KB";
  return c;
}

std::shared_ptr<wow::WorldMapCatalog> wow::WorldMapCatalog::noClient(const QString & why)
{
  std::shared_ptr<WorldMapCatalog> c(new WorldMapCatalog());
  c->m_status = Status::NoClient;
  c->m_statusDetail = why;
  return c;
}

bool wow::WorldMapCatalog::belongsToActiveClient() const
{
  if (m_status == Status::NoClient)
    return false;
  WoWFolder * folder = activeWowFolder();
  // The storage number alone would do (each folder and every storage it opens get their own); product, build and the
  // installation are compared as well, so a mismatch reads as what it is.
  return folder && GAMEDIRECTORY.clientProfile().isValid() && folder->storageGeneration() == m_client.storageGeneration &&
         GAMEDIRECTORY.clientProfile().product == m_client.product && GAMEDIRECTORY.version() == m_client.build &&
         GAMEDIRECTORY.path() == m_client.dataPath;
}

wow::StatedWdtForm wow::MapEntry::statedWdtForm() const
{
  if (!mapType.present)
    return StatedWdtForm::Unknown;
  // Codes proven against every readable WDT of the four audited clients (docs/world-viewer/02-map-catalog.md).
  switch (mapType.value)
  {
    case 1: return StatedWdtForm::TileGrid;
    case 2:
    case 4: return StatedWdtForm::WmoOnly;
    case 3: return StatedWdtForm::NoWdt;
    default: return StatedWdtForm::Unknown;
  }
}

QString wow::WorldMapCatalog::refusalReason() const
{
  if (m_status == Status::NoClient)
    return "no client was loaded when this catalogue was built";
  if (!belongsToActiveClient())
    return "the client this catalogue was built from is no longer the loaded one";
  return QString("the catalogue is not ready (%1%2)").arg(statusName(m_status),
                                                          m_statusDetail.isEmpty() ? QString() : QString(": ") + m_statusDetail);
}

bool wow::WorldMapCatalog::canRead()
{
  m_refusal.clear();
  if (m_status == Status::Ready && belongsToActiveClient())
    return true;
  m_refusal = refusalReason();
  return false;
}

const wow::MapEntry * wow::WorldMapCatalog::find(int mapId) const
{
  for (const MapEntry & e : m_maps)
    if (e.id == mapId)
      return &e;
  return nullptr;
}

bool wow::WorldMapCatalog::inspect(int mapId)
{
  if (!canRead())
    return false;
  for (MapEntry & e : m_maps)
    if (e.id == mapId)
    {
      inspectEntry(e);
      return true;
    }
  m_refusal = QString("map %1 is not in this catalogue").arg(mapId);
  return false;
}

size_t wow::WorldMapCatalog::inspectAll()
{
  if (!canRead())
    return 0;
  for (MapEntry & e : m_maps)
    inspectEntry(e);
  return m_maps.size();
}

bool wow::WorldMapCatalog::readWdt(int mapId, QByteArray & bytes, QString & why)
{
  bytes.clear();
  why.clear();
  if (!canRead())
  {
    why = m_refusal;
    return false;
  }
  MapEntry * entry = nullptr;
  for (MapEntry & e : m_maps)
    if (e.id == mapId)
      entry = &e;
  if (!entry)
  {
    why = m_refusal = QString("map %1 is not in this catalogue").arg(mapId);
    return false;
  }
  // An inspection that keeps the bytes it read: the outcome is recorded either way.
  inspectEntry(*entry, &bytes);
  if (entry->inspection.state != WdtAvailability::Available)
  {
    bytes.clear();
    why = entry->inspection.detail;
    return false;
  }
  return true;
}

void wow::WorldMapCatalog::inspectEntry(MapEntry & e, QByteArray * bytes) const
{
  QByteArray read;
  e.inspection = inspectReference(e.wdt, &read, m_hashing, 64 * 1024 * 1024);
  // A WDT is a chunked file whose first chunk is MVER, stored reversed ("REVM"). Four bytes, not a parse -- the WDT's
  // contents stay the player's parser's: it catches a reference that names another kind of file (a column read at the
  // wrong position would give one).
  if (e.inspection.state == WdtAvailability::Available && !read.startsWith("REVM"))
  {
    e.inspection.state = WdtAvailability::Unreadable;
    e.inspection.detail = QString("the file read is not a WDT: it starts with the bytes %1, not the MVER chunk")
                            .arg(QString::fromLatin1(read.left(4).toHex(' ')));
    e.inspection.contentKey.clear();
    read.clear();
  }
  if (bytes)
    *bytes = read;
}

wow::WdtInspection wow::WorldMapCatalog::inspectFile(const WdtReference & reference, QByteArray * bytes, qint64 maxBytes)
{
  if (bytes)
    bytes->clear();
  if (!canRead())
  {
    WdtInspection refused;
    refused.detail = m_refusal;
    return refused;
  }
  return inspectReference(reference, bytes, m_hashing, maxBytes);
}

wow::WdtInspection wow::WorldMapCatalog::inspectReference(const WdtReference & ref, QByteArray * keep, bool hash,
                                                          qint64 maxBytes)
{
  if (keep)
    keep->clear();
  QElapsedTimer timer;
  timer.start();
  WdtInspection r;
  WoWFolder * folder = activeWowFolder();
  if (ref.kind == WdtReference::Kind::Unresolved || (ref.kind == WdtReference::Kind::FileDataId && ref.fileDataId <= 0) ||
      (ref.kind == WdtReference::Kind::Path && ref.path.isEmpty()))
  {
    r.state = WdtAvailability::Unsupported;
    r.detail = ref.kind == WdtReference::Kind::Unresolved ? (ref.why.isEmpty() ? QString("no file reference") : ref.why)
                                                          : QString("not a valid file reference");
    r.milliseconds = timer.nsecsElapsed() / 1e6;
    return r;
  }
  if (!folder || !folder->storageOpen())
  {
    // Nothing was asked of a storage, so nothing is known about the file.
    r.detail = "the client's storage is not open: nothing was checked";
    r.milliseconds = timer.nsecsElapsed() / 1e6;
    return r;
  }

  const bool byId = ref.kind == WdtReference::Kind::FileDataId;
  const QString what = byId ? QString("FileDataID %1").arg(ref.fileDataId) : ref.path;
  // What the storage's root says, without opening the file: listed at all, and on this computer?
  const CASCFolder::RootEntry root = byId ? folder->storageRootEntry(ref.fileDataId) : folder->storageRootEntryByPath(ref.path);
  r.storageFileDataId = root.listed ? root.fileDataId : -1;
  if (!root.listed && !byId && folder->storageRootNamelessCount() > 0)
  {
    // Not "absent": the root lists files it gives no name, and this could be one of them. Only a FileDataID could tell.
    r.state = WdtAvailability::Unsupported;
    r.detail = QString("the client's storage root has no name for %1, and lists %2 files by FileDataID only: whether the "
                       "client has it cannot be told by path").arg(what).arg(folder->storageRootNamelessCount());
  }
  else if (!root.listed)
  {
    r.state = WdtAvailability::NotInClientStorage;
    r.detail = byId ? QString("the client's storage root does not list %1").arg(what)
                    : QString("the client's storage root, which names every file it lists, has no %1").arg(what);
  }
  else if (!root.local)
  {
    r.state = WdtAvailability::NotInstalledLocally;
    r.detail = QString("%1 is listed by the client's storage root but not downloaded on this computer").arg(what);
  }
  else
  {
    QByteArray bytes;
    QByteArray contentKey;
    unsigned int error = 0;
    qint64 size = -1;
    const CASCFolder::ReadOutcome read = folder->readStorageFile(byId ? ref.fileDataId : -1, byId ? QString() : ref.path,
                                                                 bytes, &error, maxBytes, &size, &contentKey);
    // What a caller that budgets reads counts. The size is known only once the storage has told it, and every outcome
    // after that but TooLarge (decided from the size, nothing read) entered the read loop: Read and Unverified read the
    // whole file; Corrupt, Encrypted, and Damaged or NotLocal from the loop failed on a frame after reading the ones
    // before it, so their count is the file's size, an upper bound. NotLocal or Damaged from the size query leave the
    // size unknown, and nothing was read.
    if (size > 0 && read != CASCFolder::ReadOutcome::TooLarge)
      r.readBytes = size;
    switch (read)
    {
      case CASCFolder::ReadOutcome::Read:
        if (bytes.isEmpty())
        {
          // Never an empty read taken as a WDT (docs/world-viewer/00-audit.md R11).
          r.state = WdtAvailability::Unreadable;
          r.detail = QString("%1 read as an empty file").arg(what);
        }
        else
        {
          r.state = WdtAvailability::Available;
          r.bytes = bytes.size();
          // The key the read checked every byte against; a Read never comes without it (readWholeFile).
          r.contentKey = QString::fromLatin1(contentKey.toHex());
          if (hash)
            r.sha256 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
          if (keep)
            *keep = bytes;
        }
        break;
      case CASCFolder::ReadOutcome::NotListed:
        r.state = WdtAvailability::NotInClientStorage;
        r.detail = QString("%1 could not be opened: the storage does not list it").arg(what);
        break;
      case CASCFolder::ReadOutcome::NotLocal:
        // The local index lists it, yet its data is not there: a damaged or half-updated install, not a choice the
        // download made.
        r.state = WdtAvailability::Unreadable;
        r.detail = QString("%1 is listed as on this computer, but its data could not be found (error %2)").arg(what).arg(error);
        break;
      case CASCFolder::ReadOutcome::Encrypted:
        r.state = WdtAvailability::Unreadable;
        r.detail = QString("%1 is encrypted with a key this computer does not have").arg(what);
        break;
      case CASCFolder::ReadOutcome::TooLarge:
        // Decided from the size the storage gives, before any byte is read.
        r.state = WdtAvailability::Unreadable;
        r.detail = QString("%1 is larger than this read allows (%2 bytes, the limit is %3)").arg(what).arg(size).arg(maxBytes);
        break;
      case CASCFolder::ReadOutcome::NoStorage:
        r.state = WdtAvailability::Unreadable;
        r.detail = "the client's storage is not open";
        break;
      case CASCFolder::ReadOutcome::Damaged:
        r.state = WdtAvailability::Unreadable;
        r.detail = QString("%1 could not be read (error %2)").arg(what).arg(error);
        break;
      case CASCFolder::ReadOutcome::Corrupt:
        r.state = WdtAvailability::Unreadable;
        r.detail = QString("%1 is damaged on this computer: its data does not match its checksums").arg(what);
        break;
      case CASCFolder::ReadOutcome::Unverified:
        // Read whole, but with no content key nothing vouches for the bytes: not handed on as the WDT.
        r.state = WdtAvailability::Unreadable;
        r.detail = QString("%1 was read, but its content key could not be obtained, so its bytes could not be checked "
                           "(error %2)").arg(what).arg(error);
        break;
    }
  }
  r.milliseconds = timer.nsecsElapsed() / 1e6;
  return r;
}

size_t wow::WorldMapCatalog::approximateBytes() const
{
  // A string's own storage (its QString is counted in its owner); an empty one shares Qt's and costs nothing more.
  auto str = [](const QString & s) -> size_t { return s.capacity() > 0 ? (size_t)s.capacity() * sizeof(QChar) + 24 : 0; };
  size_t n = sizeof(*this) + m_maps.capacity() * sizeof(MapEntry);
  n += str(m_statusDetail) + str(m_client.product) + str(m_client.build) + str(m_client.locale) + str(m_client.dataPath) +
       str(m_client.schema) + str(m_layout) + str(m_layoutState) + str(m_refusal);
  for (const QString & s : m_absentColumns)
    n += sizeof(QString) + str(s);
  for (const MapEntry & e : m_maps)
    n += str(e.directory) + str(e.name) + str(e.wdt.path) + str(e.wdt.why) + str(e.inspection.detail) +
         str(e.inspection.sha256);
  return n;
}

QJsonObject wow::WorldMapCatalog::toJson() const
{
  auto codeJson = [](const MapCode & c) { return c.present ? QJsonValue(c.value) : QJsonValue(); };
  QJsonObject o;
  QJsonObject client;
  client["product"] = m_client.product;
  client["build"] = m_client.build;
  client["locale"] = m_client.locale;
  client["dataPath"] = m_client.dataPath;
  client["schema"] = m_client.schema;
  client["storageGeneration"] = (double)m_client.storageGeneration;
  client["rootNames"] = (double)m_client.rootNames;
  client["rootNameless"] = (double)m_client.rootNameless;
  o["client"] = client;
  o["status"] = statusName(m_status);
  o["statusDetail"] = m_statusDetail;
  o["belongsToActiveClient"] = belongsToActiveClient();
  QJsonObject table;
  table["layout"] = m_layout;
  table["layoutState"] = m_layoutState;
  table["absentColumns"] = QJsonArray::fromStringList(m_absentColumns);
  table["rows"] = (double)m_rowsInTable;
  table["lockedRows"] = (double)m_lockedRows;
  o["mapTable"] = table;
  o["wdtRule"] = m_rule == WdtRule::ByFileDataId ? "by FileDataID" : m_rule == WdtRule::ByDirectoryPath ? "by directory path" : "none";
  o["queryMs"] = m_queryMs;
  o["buildMs"] = m_buildMs;
  o["approximateBytes"] = (double)approximateBytes();
  QJsonArray maps;
  for (const MapEntry & e : m_maps)
  {
    QJsonObject m;
    m["id"] = e.id;
    m["directory"] = e.directory;
    m["name"] = e.name;
    m["mapType"] = codeJson(e.mapType);
    m["instanceType"] = codeJson(e.instanceType);
    m["parentMapId"] = codeJson(e.parentMapId);
    m["worldCopyOfMapId"] = codeJson(e.worldCopyOfMapId);
    m["locationMapId"] = codeJson(e.locationMapId);
    static const char * const forms[] = {"unknown", "tile grid", "WMO only", "no WDT"};
    m["statedWdtForm"] = forms[(int)e.statedWdtForm()];
    QJsonObject wdt;
    switch (e.wdt.kind)
    {
      case WdtReference::Kind::FileDataId:
        wdt["kind"] = "FileDataID";
        wdt["fileDataId"] = e.wdt.fileDataId;
        break;
      case WdtReference::Kind::Path:
        wdt["kind"] = "path";
        wdt["path"] = e.wdt.path;
        break;
      case WdtReference::Kind::Unresolved:
        wdt["kind"] = "unresolved";
        wdt["why"] = e.wdt.why;
        break;
    }
    m["wdt"] = wdt;
    QJsonObject ins;
    ins["state"] = availabilityName(e.inspection.state);
    if (!e.inspection.detail.isEmpty())
      ins["detail"] = e.inspection.detail;
    ins["storageFileDataId"] = e.inspection.storageFileDataId;
    ins["bytes"] = (double)e.inspection.bytes;
    if (!e.inspection.sha256.isEmpty())
      ins["sha256"] = e.inspection.sha256;
    ins["ms"] = e.inspection.milliseconds;
    m["inspection"] = ins;
    maps.append(m);
  }
  o["maps"] = maps;
  return o;
}
