/*
 * LiquidCatalog.cpp
 *
 * See LiquidCatalog.h. The evidence for each column and rule is in docs/world-viewer/09-liquids.md 1.4.
 */

#include "LiquidCatalog.h"

#include <algorithm>
#include <map>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QStringList>

#include "Game.h"
#include "GameDatabase.h"
#include "logger/Logger.h"
#include "WoWDatabase.h"
#include "WoWFolder.h"

namespace
{
  // The tables as database.xml names them. LiquidType and LiquidMaterial must be read; LiquidTypeXTexture gives the frames
  // where the client has rows of it; LiquidObject is only counted (the player resolves an MH2O instance's format from its
  // own liquid type: docs/world-viewer/09-liquids.md 1.2).
  const char * const kTypeTable = "LiquidType";
  const char * const kMaterialTable = "LiquidMaterial";
  const char * const kObjectTable = "LiquidObject";
  const char * const kTextureTable = "LiquidTypeXTexture";

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

  // The names slot 0's template stands for, in frame order: a %d template frames 1 to count (the low byte of
  // FrameCountTexture[0], masked by the caller), a plain path once, an empty slot none. A name whose last part has no
  // extension gets ".blp" (Classic Era's root lists XTextures\lava\Magma0.blp, not XTextures\lava\Magma0).
  QStringList expandTemplate(const QString & stored, int count)
  {
    const QString t = stored.trimmed();
    QStringList names;
    if (t.isEmpty())
      return names;
    const auto withExtension = [](const QString & name) {
      const int slash = std::max(name.lastIndexOf('\\'), name.lastIndexOf('/'));
      return name.indexOf('.', slash + 1) < 0 ? name + ".blp" : name;
    };
    if (t.contains("%d"))
    {
      for (int k = 1; k <= count; k++)
        names << withExtension(QString(t).replace("%d", QString::number(k)));
    }
    else
      names << withExtension(t);
    return names;
  }

  // Does a texture's file name (its path's last part) contain "bump", in any case? The only sign of a ripple normal map
  // the tables give (INFERRED: Retail's water types name XTextures\ocean\newoceanbump.blp in slot 2, a unit normal map).
  bool namesBump(const QString & path)
  {
    const int slash = std::max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
    return path.mid(slash + 1).contains("bump", Qt::CaseInsensitive);
  }
}

QString wow::LiquidCatalog::statusName(Status s)
{
  switch (s)
  {
    case Status::NoClient: return "no client";
    case Status::Unavailable: return "unavailable";
    case Status::Ready: return "ready";
  }
  return "unknown";
}

std::shared_ptr<wow::LiquidCatalog> wow::LiquidCatalog::noClient(const QString & why)
{
  std::shared_ptr<LiquidCatalog> c(new LiquidCatalog());
  c->m_status = Status::NoClient;
  c->m_statusDetail = why;
  c->makeJson();
  return c;
}

std::shared_ptr<wow::LiquidCatalog> wow::LiquidCatalog::build()
{
  QElapsedTimer total;
  total.start();
  std::shared_ptr<LiquidCatalog> c(new LiquidCatalog());
  const auto finish = [&c, &total](Status status, const QString & detail) {
    c->m_status = status;
    c->m_statusDetail = detail;
    c->m_buildMs = total.nsecsElapsed() / 1e6;
    c->makeJson();
    if (status != Status::Ready)
      LOG_WARNING << "[liquids] catalogue:" << c->m_product.toUtf8().constData() << c->m_build.toUtf8().constData()
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
                                         ? QString("a legacy MPQ client: its liquids are in DBC tables this viewer does not read")
                                         : QString("the loaded client has no game storage this catalogue reads"));
  WoWDatabase * db = dynamic_cast<WoWDatabase *>(&GAMEDATABASE);
  // The database's schema check must be this client's: one that failed to open keeps none.
  if (!db || db->schemaCheck().build != c->m_build || db->schemaCheck().product != c->m_product)
    return finish(Status::Unavailable, "this client's database was not read");

  // How each table was read, and its rows here.
  const char * const names[] = {kTypeTable, kMaterialTable, kObjectTable, kTextureTable};
  for (const char * name : names)
  {
    const WoWDatabase::SchemaCheck::TableReading * reading = db->tableReading(name);
    Table t;
    t.name = QString::fromLatin1(name);
    t.layout = reading ? reading->layout : QString();
    t.state = readingState(reading);
    t.read = readable(reading);
    t.rows = t.read ? countRows(name) : -1;
    c->m_tables.push_back(t);
  }
  const Table & types = c->m_tables[0];
  const Table & materials = c->m_tables[1];
  const Table & textures = c->m_tables[3];
  if (!types.read || types.rows < 0)
    return finish(Status::Unavailable, "LiquidType.db2 is " + (types.read ? QString("not queryable") : types.state));
  if (!materials.read || materials.rows < 0)
    return finish(Status::Unavailable, "LiquidMaterial.db2 is " + (materials.read ? QString("not queryable") : materials.state));

  // The types and their materials, and slots 1-5's templates and frame counts (columns 12-16 and 17-21). Every value is
  // masked to its column's width, as database.xml declares it (the database keeps those bytes alone already): a pallet
  // word's constant upper bits are not part of the value.
  const sqlResult rows = GAMEDATABASE.sqlQuery(
    QString("SELECT t.ID, t.Name, t.Texture1, t.SoundBank & 255, t.MaterialID & 255, t.MinimapStaticCol, "
            "t.FrameCountTexture1 & 255, t.Float1, t.Float2, m.ID, m.Flags, m.LVF, "
            "t.Texture2, t.Texture3, t.Texture4, t.Texture5, t.Texture6, t.FrameCountTexture2 & 255, "
            "t.FrameCountTexture3 & 255, t.FrameCountTexture4 & 255, t.FrameCountTexture5 & 255, "
            "t.FrameCountTexture6 & 255 FROM %1 t LEFT JOIN %2 m ON m.ID = (t.MaterialID & 255) ORDER BY t.ID")
      .arg(kTypeTable, kMaterialTable));
  if (!rows.valid)
    return finish(Status::Unavailable, "LiquidType.db2's table could not be queried");

  // LiquidTypeXTexture's rows by liquid type, in OrderIndex order: (FileDataID, OrderIndex).
  std::map<int, std::vector<std::pair<int, int>>> rowsByType;
  if (textures.read && textures.rows > 0)
  {
    const sqlResult t = GAMEDATABASE.sqlQuery(QString("SELECT LiquidTypeID, OrderIndex, FileDataID FROM %1 "
                                                      "ORDER BY LiquidTypeID, OrderIndex, ID").arg(kTextureTable));
    if (!t.valid)
      return finish(Status::Unavailable, "LiquidTypeXTexture.db2's table could not be queried");
    for (const std::vector<QString> & r : t.values)
    {
      if (r.size() < 3)
        continue;
      bool ok = false;
      const int type = r[0].toInt(&ok);
      if (ok)
        rowsByType[type].push_back(std::make_pair(r[2].toInt(), r[1].toInt()));
    }
  }

  // The storage root's names: what Classic Era's frames are found by, and what checks the table's rows where the root
  // names files (a root that names none -- Retail, Classic Beta -- is not asked: it would find nothing).
  const bool rootNames = folder->storageRootNameCount() > 0;
  const auto byName = [folder](const QString & name) {
    const CASCFolder::RootEntry e = folder->storageRootEntryByPath(name);
    return e.listed && e.fileDataId > 0 ? e.fileDataId : 0;
  };

  std::vector<int> all, bumps;
  c->m_types.reserve(rows.values.size());
  for (const std::vector<QString> & r : rows.values)
  {
    if (r.size() < 22)
      continue;
    LiquidTypeEntry e;
    e.id = r[0].toInt();
    e.name = r[1];
    e.frameTemplate = r[2];
    bool ok = false;
    const int kind = r[3].toInt(&ok);
    e.kind = ok ? kind : -1;
    e.material = r[4].toInt();
    e.minimapColor = (quint32)r[5].toLongLong();
    const int count = r[6].toInt() & 0xFF;
    e.float0 = r[7].toDouble();
    e.float1 = r[8].toDouble();
    // No LiquidMaterial row of that ID (the join found none): flags and format absent, never 0.
    if (!r[9].isEmpty())
    {
      e.materialFlags = r[10].toInt();
      e.lvf = r[11].toInt();
    }
    const QStringList expected = expandTemplate(e.frameTemplate, count);
    e.framesWanted = expected.size();
    const auto own = rowsByType.find(e.id);
    if (own != rowsByType.end())
    {
      // The type's first framesWanted rows are slot 0's frames (the rows are the six templates expanded in slot order).
      // A row that is no file (FileDataID 0: a procedural texture's) is not a frame.
      const std::vector<std::pair<int, int>> & list = own->second;
      for (int k = 0; k < e.framesWanted; k++)
      {
        const int fdid = k < (int)list.size() ? list[k].first : 0;
        if (fdid <= 0)
          continue;
        e.frames.push_back(fdid);
        if (rootNames)
        {
          const int named = byName(expected[k]);
          if (named > 0 && named == fdid)
            c->m_checkedByName++;
          else if (named > 0)
          {
            c->m_disagreeing++;
            LOG_WARNING << "[liquids] liquid type" << e.id << "frame" << k + 1 << ": LiquidTypeXTexture names FileDataID" << fdid
                        << "but the storage root gives" << expected[k].toUtf8().constData() << "FileDataID" << named
                        << "-- the table's kept";
          }
        }
      }
      c->m_typesFromTable++;
    }
    else if (e.framesWanted > 0)
    {
      // No rows of the type (Classic Era has none of any): its template's names, each looked up in the storage's root.
      e.framesByName = true;
      if (rootNames)
        for (const QString & name : expected)
        {
          const int fdid = byName(name);
          if (fdid > 0)
            e.frames.push_back(fdid);
        }
      c->m_typesFromNames++;
    }
    e.framesUnresolved = e.framesWanted - (int)e.frames.size();
    c->m_unresolved += e.framesUnresolved;
    c->m_frameCount += e.frames.size();
    all.insert(all.end(), e.frames.begin(), e.frames.end());
    // THE BUMP: the first file of slots 1-5 whose name contains "bump". With rows of the type, the rows after slot 0's
    // frames are slots 1-5's in slot order, each named by the listfile (a procedural slot's row is no file); without, each
    // slot's template expanded as slot 0's and looked up in the storage's root, a name it does not list counted.
    if (own != rowsByType.end())
    {
      const std::vector<std::pair<int, int>> & list = own->second;
      for (size_t k = (size_t)e.framesWanted; k < list.size() && e.bump == 0; k++)
        if (list[k].first > 0 && namesBump(folder->fileName(list[k].first)))
          e.bump = list[k].first;
    }
    else
    {
      for (int slot = 1; slot <= 5 && e.bump == 0; slot++)
      {
        if (!namesBump(r[11 + slot]))
          continue;
        for (const QString & name : expandTemplate(r[11 + slot], r[16 + slot].toInt() & 0xFF))
        {
          const int fdid = rootNames ? byName(name) : 0;
          if (fdid > 0)
          {
            e.bump = fdid;
            break;
          }
          c->m_unresolvedBumps++;
        }
      }
    }
    if (e.bump > 0)
    {
      c->m_typesWithBump++;
      bumps.push_back(e.bump);
    }
    c->m_types.push_back(std::move(e));
  }
  std::sort(all.begin(), all.end());
  all.erase(std::unique(all.begin(), all.end()), all.end());
  std::sort(bumps.begin(), bumps.end());
  bumps.erase(std::unique(bumps.begin(), bumps.end()), bumps.end());
  c->m_bumpCount = bumps.size();
  std::vector<int> servable(all);
  servable.insert(servable.end(), bumps.begin(), bumps.end());
  std::sort(servable.begin(), servable.end());
  servable.erase(std::unique(servable.begin(), servable.end()), servable.end());
  c->m_frameIds = std::make_shared<const std::vector<int>>(std::move(all));
  c->m_textureIds = std::make_shared<const std::vector<int>>(std::move(servable));

  // LiquidObject's LiquidTypeID, masked to its 16 bits: every one a LiquidType ID in the four audited clients.
  if (c->m_tables[2].read)
  {
    const sqlResult o = GAMEDATABASE.sqlQuery(
      QString("SELECT COUNT(*) FROM %1 o LEFT JOIN %2 t ON t.ID = (o.LiquidTypeID & 65535) WHERE t.ID IS NULL")
        .arg(kObjectTable, kTypeTable));
    if (o.valid && !o.values.empty() && !o.values[0].empty())
      c->m_objectsWithoutType = o.values[0][0].toLongLong();
  }

  QString notes;
  if (!textures.read)
    notes = "LiquidTypeXTexture.db2 is " + textures.state + ": every type's frames are found by name";
  if (c->m_unresolved > 0)
    notes += (notes.isEmpty() ? QString() : QString("; ")) +
             QString("%1 frame(s) could not be found in the client's storage").arg(c->m_unresolved);
  if (c->m_unresolvedBumps > 0)
    notes += (notes.isEmpty() ? QString() : QString("; ")) +
             QString("%1 bump name(s) could not be found in the client's storage").arg(c->m_unresolvedBumps);
  finish(Status::Ready, notes);

  QStringList tableLines;
  for (const Table & t : c->m_tables)
    tableLines << QString("%1 %2 row(s) (layout %3, %4)").arg(t.name).arg(t.rows).arg(t.layout.isEmpty() ? QString("-") : t.layout)
                    .arg(t.state);
  LOG_INFO << "[liquids] catalogue:" << c->m_product.toUtf8().constData() << c->m_build.toUtf8().constData() << ":"
           << (unsigned int)c->m_types.size() << "types," << (unsigned int)c->m_frameCount << "frames," << c->m_unresolved
           << "unresolved," << c->m_buildMs << "ms; frames from LiquidTypeXTexture for" << c->m_typesFromTable
           << "type(s), by name for" << c->m_typesFromNames << "; checked by name" << c->m_checkedByName << ", disagreeing"
           << c->m_disagreeing << "; bumps for" << c->m_typesWithBump << "type(s)," << (unsigned int)c->m_bumpCount
           << "distinct," << c->m_unresolvedBumps << "unresolved; LiquidObject rows without a liquid type"
           << (int)c->m_objectsWithoutType << "; tables"
           << tableLines.join(", ").toUtf8().constData();
  return c;
}

bool wow::LiquidCatalog::belongsToActiveClient() const
{
  if (m_status == Status::NoClient)
    return false;
  WoWFolder * folder = activeWowFolder();
  return folder && GAMEDIRECTORY.clientProfile().isValid() && folder->storageGeneration() == m_storageGeneration &&
         GAMEDIRECTORY.clientProfile().product == m_product && GAMEDIRECTORY.version() == m_build &&
         GAMEDIRECTORY.path() == m_dataPath;
}

const wow::LiquidTypeEntry * wow::LiquidCatalog::find(int id) const
{
  for (const LiquidTypeEntry & e : m_types)
    if (e.id == id)
      return &e;
  return nullptr;
}

bool wow::LiquidCatalog::isFrame(int fileDataID) const
{
  return std::binary_search(m_frameIds->begin(), m_frameIds->end(), fileDataID);
}

bool wow::LiquidCatalog::isTexture(int fileDataID) const
{
  return std::binary_search(m_textureIds->begin(), m_textureIds->end(), fileDataID);
}

const wow::LiquidCatalog::Table * wow::LiquidCatalog::table(const QString & name) const
{
  for (const Table & t : m_tables)
    if (t.name == name)
      return &t;
  return nullptr;
}

QJsonObject wow::LiquidCatalog::unavailableJson(const QString & product, const QString & build, const QString & why)
{
  QJsonObject o;
  o["product"] = product;
  o["build"] = build;
  o["state"] = "unavailable";
  o["detail"] = why;
  o["types"] = QJsonArray();
  return o;
}

void wow::LiquidCatalog::makeJson()
{
  if (m_status != Status::Ready)
  {
    m_json = unavailableJson(m_product, m_build, m_statusDetail);
    return;
  }
  QJsonArray types;
  for (const LiquidTypeEntry & e : m_types)
  {
    QJsonObject t;
    t["id"] = e.id;
    t["name"] = e.name;
    t["kind"] = e.kind;
    t["material"] = e.material;
    t["materialFlags"] = e.materialFlags;
    t["lvf"] = e.lvf;
    t["minimapColor"] = (double)e.minimapColor;
    t["float0"] = e.float0;
    t["float1"] = e.float1;
    QJsonArray frames;
    for (int f : e.frames)
      frames.append(f);
    t["frames"] = frames;
    t["framesWanted"] = e.framesWanted;
    t["bump"] = e.bump;
    types.append(t);
  }
  QJsonObject o;
  o["product"] = m_product;
  o["build"] = m_build;
  o["state"] = "ok";
  o["detail"] = m_statusDetail;
  o["types"] = types;
  m_json = o;
}
