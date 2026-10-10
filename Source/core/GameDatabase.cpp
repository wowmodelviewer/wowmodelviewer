/*
 * GameDatabase.cpp
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#include "GameDatabase.h"

#include "dbfile.h"
#include "CSVFile.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include <exception>

#include "logger/Logger.h"
#include "Game.h"
#include "LoadTimeline.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

core::GameDatabase::~GameDatabase()
{
  closeDatabase();
}


core::GameDatabase::GameDatabase()
: m_db(NULL), m_fastMode(true) // cache the built DB to disk and reuse it across launches (see initFromXML)
{

}

namespace
{
  // THE DATABASE CACHE. Each client's database is built once from its DB2 tables (~20 s for Retail) and kept, one file
  // per client, in dbcache/ (relative to the working directory, like listfile.csv and dbd/): a switch to another
  // client and back opens the kept file again instead of building it a second time.
  //
  // A file is named after its client (wowdb-<product>-<build>-<key hash>.sqlite) and holds its full key in a table of
  // its own (wmv_cache): the build, the product, the schema folder, the schema version, the locale and a hash of every
  // file the tables are built from (the schema folder's database.xml and CSVs, the game definitions). It is used only
  // when that key is this client's and it was marked complete.
  //
  // It is built into a file of this process's own (.building-<pid>) and published under the client's name only when
  // the build is complete -- every table that this client has on this computer was read, none failed. A client still
  // downloading (a table listed but not on this computer yet), or a build with a failed table, keeps its database for
  // the session only: it is built again next time. Publishing replaces the file at once (a rename), so another
  // process never sees half of one.
  //
  // The CACHE_KEEP files used last are kept; older ones (and the builds a process left behind) are deleted when a
  // database is used. A file another process has open is left alone.
  const char * CACHE_DIR = "dbcache";
  const int CACHE_KEEP = 6;
  const int CACHE_FORMAT = 1; // the layout of wmv_cache; part of the key
  const char * LEGACY_DB = "./wowdb.sqlite";           // the single cache of earlier versions
  const char * LEGACY_STAMP = "./wowdb.sqlite.build";

  QString sanitized(const QString & s)
  {
    QString out;
    for (const QChar c : s)
      out += (c.isLetterOrNumber() || c == '.' || c == '_') ? c : QChar('_');
    return out;
  }

  // The name and content of every file of a folder, in name order, into a hash.
  void hashFolder(QCryptographicHash & hash, const QString & folder, const QStringList & filters)
  {
    QDir dir(folder);
    for (const QString & name : dir.entryList(filters, QDir::Files, QDir::Name))
    {
      QFile f(dir.filePath(name));
      if (!f.open(QIODevice::ReadOnly))
        continue;
      hash.addData(name.toUtf8());
      hash.addData(f.readAll());
    }
  }

  // The key and its value from a database's wmv_cache table; empty when it has none.
  QString cacheValue(sqlite3 * db, const char * name)
  {
    QString value;
    sqlite3_stmt * stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT value FROM wmv_cache WHERE name = ?", -1, &stmt, nullptr) != SQLITE_OK)
      return value;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW)
      value = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    return value;
  }

  bool setCacheValue(sqlite3 * db, const char * name, const QByteArray & value)
  {
    sqlite3_stmt * stmt = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO wmv_cache(name, value) VALUES(?, ?)", -1, &stmt, nullptr) != SQLITE_OK)
      return false;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value.constData(), value.size(), SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
  }

  // The file replaces dest at once (or not at all): another process opening dest sees the old file or the new one.
  bool replaceFile(const QString & source, const QString & dest)
  {
#ifdef _WIN32
    return MoveFileExW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(source).utf16()),
                       reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(dest).utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return ::rename(source.toLocal8Bit().constData(), dest.toLocal8Bit().constData()) == 0;
#endif
  }

  // A file's data written out to the disk (not just to the system's cache).
  void flushFile(const QString & path)
  {
#ifdef _WIN32
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
      FlushFileBuffers(h);
      CloseHandle(h);
    }
#else
    Q_UNUSED(path);
#endif
  }

  // The cache files beyond the CACHE_KEEP used last, and builds another process left behind, are deleted (a file in
  // use stays: Windows does not delete an open file). The one in use now is always kept.
  void pruneCache(const QString & inUse)
  {
    QDir dir(CACHE_DIR);
    const QString own = QString(".building-%1.").arg(QCoreApplication::applicationPid());
    QFileInfoList files = dir.entryInfoList(QStringList() << "wowdb-*.sqlite", QDir::Files);
    std::sort(files.begin(), files.end(),
              [](const QFileInfo & a, const QFileInfo & b) { return a.lastModified() > b.lastModified(); });
    int kept = 0;
    for (const QFileInfo & f : files)
    {
      const QString name = f.fileName();
      if (name.contains(".building-"))
      {
        // Another session's build: deleted once it is an hour old (Windows refuses while it is open; between its
        // close and its publish it is not, and it must not go then).
        if (!name.contains(own) && f.lastModified().secsTo(QDateTime::currentDateTime()) > 3600 &&
            QFile::remove(f.filePath()))
          LOG_INFO << "Database cache: deleted" << name << "(a build another session did not finish)";
        continue;
      }
      if (f.absoluteFilePath() == QFileInfo(inUse).absoluteFilePath() || kept < CACHE_KEEP)
      {
        kept++;
        continue;
      }
      if (QFile::remove(f.filePath()))
        LOG_INFO << "Database cache: deleted" << name << "(not used recently)";
    }
    // The single cache of earlier versions, once.
    if (QFile::exists(LEGACY_DB) && QFile::remove(LEGACY_DB))
      LOG_INFO << "Database cache: deleted the single cache of earlier versions" << LEGACY_DB;
    QFile::remove(LEGACY_STAMP);
  }
}

QString core::GameDatabase::cacheKey() const
{
  // Bump SCHEMA_VERSION whenever the table layout in database.xml (or how we read it) changes.
  static const int SCHEMA_VERSION = 17; // 17: TransmogSet and TransmogSetItem (the transmog sets of the set picker, with their catalog group and class mask). 16: ChrModelAltVariant, ChrModelAltVariantOption, ChrModelAltVariantChoice (Classic Beta's two generations of a character model). 15: CreatureDisplayInfoExtra and CreatureDisplayInfoOption (a humanoid NPC's own race, sex, class, bakes and stored customization choices). 14: the key also names the product and the schema folder (a Classic
                                        // product could share a build number with nothing, but a cache built
                                        // with one schema must never be read as another's); tables whose layout
                                        // the definitions cannot place are no longer read at base positions; a table's own layoutHash in database.xml is read (it never was). // 13: ChrCustomizationReq RaceMasks as its two uint32 (plus ReqType/RegionGroupMask/OverrideArchive), ChrCustomizationOption.Requirement, ChrRaces.PlayableRaceBit, ChrCustomizationReqChoice; bitpacked signed values sign-extended, pallet arrays read at the file's stride, short ids/values read without over-reading their bytes. 12: ItemDisplayInfoModelMatRes (retail's per-slot replaceable-material table, the authoritative source for M2 texture types beyond the type-2 skin). 11: ChrCustomizationReq adds ReqAchievementID/ReqQuestID/ReqItemModifiedAppearanceID (unlock-gate filter for customization choices). 10: corrected ItemSparse name-field positions (sparse-record string walk) for 12.0.7. 9: ChrCustomizationReq/ChrRaces/CreatureDisplayInfo/CreatureModelData. Bump forces a cache rebuild so the fix reaches installs upgraded over a prior build
  const QString build = GAMEDIRECTORY.version(); // e.g. "12.1.0.69933"
  if (build.isEmpty())
    return QString(); // a database of an unknown build is never kept
  const QString schemaFolder = core::Game::instance().configFolder();
  QCryptographicHash inputs(QCryptographicHash::Sha1);
  hashFolder(inputs, schemaFolder, QStringList() << "*.xml" << "*.csv");
  inputs.addData(cacheInputs());
  return build + "|" + GAMEDIRECTORY.clientProfile().product + "|" + schemaFolder + "|schema" +
         QString::number(SCHEMA_VERSION) + "|" + GAMEDIRECTORY.locale() + "|format" + QString::number(CACHE_FORMAT) +
         "|inputs " + QString::fromLatin1(inputs.result().toHex().left(16));
}

void core::GameDatabase::closeDatabase()
{
  if (m_db)
  {
    sqlite3_close_v2(m_db);
    m_db = NULL;
  }
  // A database kept for one session only (a client still downloading) goes with it.
  if (!m_sessionFile.isEmpty())
  {
    QFile::remove(m_sessionFile);
    m_sessionFile.clear();
  }
}

bool core::GameDatabase::openDatabase(const QString & path, bool building)
{
  // Only a build makes a file: a database that should be there and is not (another process removed it) fails here
  // rather than opening empty.
  const int flags = SQLITE_OPEN_READWRITE | (building ? SQLITE_OPEN_CREATE : 0);
  if (sqlite3_open_v2(path.toUtf8().constData(), &m_db, flags, nullptr) != SQLITE_OK)
  {
    LOG_ERROR << "Can't open database" << path << ":" << sqlite3_errmsg(m_db);
    sqlite3_close_v2(m_db);
    m_db = NULL;
    return false;
  }
  if (building)
  {
    // The build does thousands of chunked INSERTs; with the default rollback journal + synchronous=FULL each commit
    // fsyncs. A build that does not finish is never used (it is published only when complete), so durability is
    // irrelevant.
    sqlite3_exec(m_db, "PRAGMA synchronous=OFF; PRAGMA journal_mode=MEMORY; PRAGMA temp_store=MEMORY;", nullptr,
                 nullptr, nullptr);
  }
  // Performance: memory-map the database and give SQLite a big page cache so the many small lookups each action
  // fires (customization choices, item/equipment display info, texture/section composition) are served from RAM.
  // A database is ~60-130 MB, so a 512 MB mmap window covers it entirely.
  sqlite3_exec(m_db,
    "PRAGMA mmap_size=536870912;"   // 512 MB memory-mapped I/O
    "PRAGMA cache_size=-131072;",   // 128 MB page cache (negative = KiB)
    nullptr, nullptr, nullptr);
  sqlite3_profile(m_db, GameDatabase::logQueryTime, m_db);
  return true;
}

bool core::GameDatabase::initFromXML(const QString & file)
{
  // Loading a client again comes back to this same database: the previous client's connection is closed first.
  closeDatabase();

  core::LoadTimeline & timeline = core::LoadTimeline::instance();
  timeline.begin("cache check");
  QDir().mkpath(CACHE_DIR);
  const QString key = cacheKey();
  const QString name = key.isEmpty() ? QString()
                                     : QString("wowdb-%1-%2-%3")
                                         .arg(sanitized(GAMEDIRECTORY.clientProfile().product))
                                         .arg(sanitized(GAMEDIRECTORY.version()))
                                         .arg(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(10)));
  const QString cached = name.isEmpty() ? QString() : QString(CACHE_DIR) + "/" + name + ".sqlite";

  // THE CLIENT'S OWN DATABASE, when it was kept and is still its own.
  if (!cached.isEmpty() && QFile::exists(cached))
  {
    QString why;
    {
      // Used now: the last to be deleted (pruneCache goes by this time).
      QFile touch(cached);
      if (touch.open(QIODevice::ReadWrite))
        touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    }
    if (openDatabase(cached, false))
    {
      const QString storedKey = cacheValue(m_db, "key");
      if (storedKey.isEmpty())
        why = "not a database cache of this version (no key)";
      else if (storedKey != key)
        why = "built for " + storedKey;
      else if (cacheValue(m_db, "complete") != "1")
        why = "marked incomplete";
      else if (!restoreCacheState(cacheValue(m_db, "state").toUtf8()))
        why = "its schema check could not be read";
      if (why.isEmpty())
      {
        LOG_INFO << "Reusing cached database for" << key << "from" << cached << "(skipping DB2 rebuild)";
        timeline.cache("database", "HIT", name);
        timeline.end("cache check");
        // CREATE INDEX IF NOT EXISTS: nothing to write on a cache that has them; an index added since it was built is
        // added now, as it always was on a reused cache.
        timeline.begin("indexes");
        createIndices();
        timeline.end("indexes");
        pruneCache(cached);
        return true;
      }
      closeDatabase();
    }
    else
      why = "could not be opened";
    LOG_INFO << "Database cache" << cached << "not used:" << why << "- rebuilding from DB2";
    timeline.cache("database", "REBUILD", name + ": " + why);
  }
  else
  {
    LOG_INFO << "Database cache missing for" << key << "- building from DB2";
    timeline.cache("database", "MISS", key.isEmpty() ? QString("the build is unknown: not kept") : "no " + name);
  }
  timeline.end("cache check");

  // THE BUILD, into a file of this process's own.
  const QString building = QString(CACHE_DIR) + "/" + (name.isEmpty() ? QString("wowdb-unknown") : name) +
                           QString(".building-%1.sqlite").arg(QCoreApplication::applicationPid());
  QFile::remove(building);
  if (!openDatabase(building, true))
    return false;
  m_sessionFile = building; // until it is published
  LOG_INFO << "Opened database successfully";

  // One transaction for the whole build: the tables are written once, at the end.
  sqlite3_exec(m_db, "BEGIN;", nullptr, nullptr, nullptr);
  m_failedTables = 0;
  const bool ok = createDatabaseFromXML(core::Game::instance().configFolder() + file);
  sqlite3_exec(m_db, "CREATE TABLE IF NOT EXISTS wmv_cache(name TEXT PRIMARY KEY, value TEXT);", nullptr, nullptr, nullptr);
  const bool complete = ok && cacheComplete() && m_failedTables == 0 && !key.isEmpty();
  setCacheValue(m_db, "key", key.toUtf8());
  setCacheValue(m_db, "state", saveCacheState());
  setCacheValue(m_db, "complete", complete ? "1" : "0");
  setCacheValue(m_db, "built", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
  // Most of the build is written here (one transaction): a full disk or an I/O error shows at the commit, which then
  // rolls the whole build back. Such a build is neither used nor kept.
  char * commitError = nullptr;
  const int committed = sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, &commitError);
  if (committed != SQLITE_OK)
  {
    LOG_ERROR << "Database build could not be written:" << (commitError ? commitError : sqlite3_errstr(committed));
    sqlite3_free(commitError);
    closeDatabase(); // and the build file with it
    return false;
  }

  core::LoadTimeline::Stage publish("cache publish");
  if (!complete)
  {
    if (!cacheComplete())
      LOG_INFO << "Database cache not stamped: tables of this client are not on this computer yet (rebuilt next time)";
    else if (m_failedTables > 0)
      LOG_WARNING << "Database cache not kept:" << m_failedTables << "table(s) could not be built (rebuilt next time)";
    publish.note("kept for this session only");
    pruneCache(building);
    return ok;
  }
  // Published under the client's name, then used from there -- on disk first (it was written without syncing), so a
  // file trusted for good is never one a power cut left half written.
  sqlite3_close_v2(m_db);
  m_db = NULL;
  flushFile(building);
  if (replaceFile(building, cached))
  {
    m_sessionFile.clear();
    publish.note("kept as " + name);
    LOG_INFO << "Database cache kept as" << cached;
    const bool reopened = openDatabase(cached, false);
    pruneCache(cached);
    return ok && reopened;
  }
  // The client's file is in use by another process (an export this viewer started, a second viewer) and holds a
  // database that was not this one's: this session keeps its own build.
  LOG_WARNING << "Database cache" << cached << "could not be replaced (in use by another process?) - this session keeps"
              << "its own build";
  publish.note("kept for this session only (the cache file is in use)");
  const bool reopened = openDatabase(building, false);
  if (!reopened)
    m_sessionFile.clear();
  pruneCache(building);
  return ok && reopened;
}

sqlResult core::GameDatabase::sqlQuery(const QString & query)
{
  sqlResult result;

  char *zErrMsg = 0;
  int rc = sqlite3_exec(m_db, query.toStdString().c_str(), core::GameDatabase::treatQuery, (void *)&result, &zErrMsg);
  if( rc != SQLITE_OK )
  {
    LOG_ERROR << "Querying in database" << query;
    LOG_ERROR << "SQL error:" << zErrMsg;
    sqlite3_free(zErrMsg);
    result.valid = false;
  }
  else
  {
    result.valid = true; // result is valid
  }

  return result;
}

void core::GameDatabase::addTable(TableStructure * tbl)
{
  m_dbStruct.push_back(tbl);
}

int core::GameDatabase::treatQuery(void *resultPtr, int nbcols, char ** vals , char ** cols)
{
  sqlResult * r = (sqlResult *)resultPtr;
  if(!r)
    return 1;

  std::vector<QString> values;
  // update columns
  for(int i=0; i<nbcols; i++)
  {
    values.push_back(QString(vals[i]));
  }

  r->values.push_back(values);
  r->nbcols = nbcols;

  return 0;
}

bool core::GameDatabase::createDatabaseFromXML(const QString & file)
{
  core::LoadTimeline::instance().begin("schema read");
  const bool structureRead = readStructureFromXML(file);
  core::LoadTimeline::instance().end("schema read", QString("%1 tables").arg(m_dbStruct.size()));
  if (!structureRead)
  {
    LOG_ERROR << "Reading database structure from XML file failed ! Impossible to create database.";
    return false;
  }

  // Let the game-specific database adapt the structures to the loaded build
  // (e.g. refresh DB2 field positions from WoWDBDefs).
  core::LoadTimeline::instance().begin("table layouts");
  refreshStructures(m_dbStruct);
  core::LoadTimeline::instance().end("table layouts");

  bool result = true; // ok until we found an issue
  core::LoadTimeline::instance().begin("tables");
  int built = 0, kept = 0;
  m_buildTables = m_dbStruct.size();
  m_buildTable = 0;

  for (auto it = m_dbStruct.begin(), itEnd = m_dbStruct.end(); it != itEnd; ++it)
  {
    // Build each table defensively: a single table that fails (e.g. an
    // unsupported layout on a newer build, or a bad alloc on a very large
    // sparse table) must not abort the whole database or crash the app -- the
    // other tables (and the model the user wants) can still load.
    m_buildTable = (size_t)(it - m_dbStruct.begin());
    reportTableFill(0.0);
    try
    {
      if ((*it)->create())
      {
        built++;
        if ((*it)->skipFill)
          LOG_WARNING << "Table" << (*it)->name << "not read:" << (*it)->skipReason;
        else if (!(*it)->fill())
        {
          // The database is still used for this session, but a cache missing a table is never kept.
          m_failedTables++;
          LOG_WARNING << "Error during table filling" << (*it)->name;
          if (!m_fastMode)
            result = false;
        }
      }
      else
      {
        // Every build is into a new file of its own, so a table that cannot be created is a failed one (it used to be
        // one that already existed in the reused cache).
        kept++;
        m_failedTables++;
        if (!m_fastMode) // if table already exists in fast mode, continue
        {
          LOG_ERROR << "Error during table creation" << (*it)->name;
          result = false;
        }
      }
    }
    catch (const std::exception & e)
    {
      m_failedTables++;
      LOG_ERROR << "Exception while building table" << (*it)->name << ":" << e.what() << "- skipping table";
    }
    catch (...)
    {
      m_failedTables++;
      LOG_ERROR << "Unknown exception while building table" << (*it)->name << "- skipping table";
    }
  }

  // Empty the list as well as freeing it: loading a client again reads the structures into this
  // same list, and the pointers left behind were the first thing the WoWDBDefs refresh touched.
  for (auto it : m_dbStruct)
    delete it;
  m_dbStruct.clear();
  core::LoadTimeline::instance().end("tables", QString("%1 built, %2 not created").arg(built).arg(kept));

  // All tables are populated (or reused from cache) -- create the secondary indexes on
  // the hot join/lookup columns. Idempotent, so this is cheap on an already-indexed cache.
  core::LoadTimeline::instance().begin("indexes");
  createIndices();
  core::LoadTimeline::instance().end("indexes");

  return result;
}

void core::GameDatabase::reportTableFill(double share)
{
  if (m_buildProgress && m_buildTables > 0)
    m_buildProgress((float)((m_buildTable + share) / (double)m_buildTables));
}

void core::GameDatabase::logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs)
{
  if(aTimeInNs/1000000 > 50)
  {
    LOG_WARNING << "LONG QUERY !";
    LOG_WARNING << aQueryStr;
    LOG_WARNING << "Query time (ms)" << aTimeInNs / 1000000;
  }

}

bool core::GameDatabase::readStructureFromXML(const QString & file)
{
  QDomDocument doc;

  QFile f(file);
  f.open(QIODevice::ReadOnly);
  doc.setContent(&f);
  f.close();

  QDomElement docElem = doc.documentElement();

  QDomElement e = docElem.firstChildElement();

  while (!e.isNull())
  {
    core::TableStructure * tblStruct = createTableStructure();
    QDomElement child = e.firstChildElement();

    QDomNamedNodeMap attributes = e.attributes();
    QDomNode dbfile = attributes.namedItem("dbfile");

    // table values
    tblStruct->name = attributes.namedItem("name").nodeValue();

    if (!dbfile.isNull())
      tblStruct->file = dbfile.nodeValue();
    else
      tblStruct->file = tblStruct->name;

    readSpecificTableAttributes(e, tblStruct); // the table's own attributes (it was given its first field's)

    int fieldId = 0;
    while (!child.isNull())
    {
      core::FieldStructure * fieldStruct = createFieldStructure();
      fieldStruct->id = fieldId;
      QDomNamedNodeMap Attributes = child.attributes();

      // search if name and type are here
      QDomNode name = Attributes.namedItem("name");
      QDomNode type = Attributes.namedItem("type");
      QDomNode key = Attributes.namedItem("primary");
      QDomNode arraySize = Attributes.namedItem("arraySize");
      QDomNode index = Attributes.namedItem("createIndex");

      if (!name.isNull() && !type.isNull())
      {
        fieldStruct->name = name.nodeValue();
        fieldStruct->type = type.nodeValue();

        if (!key.isNull())
          fieldStruct->isKey = true;

        if (!index.isNull())
          fieldStruct->needIndex = true;

        if (!arraySize.isNull())
          fieldStruct->arraySize = arraySize.nodeValue().toUInt();

        readSpecificFieldAttributes(child, fieldStruct);

        tblStruct->fields.push_back(fieldStruct);
      }

      fieldId++;
      child = child.nextSiblingElement();
    }

    /*
    LOG_INFO << "----------------------------";
    LOG_INFO << "Table" << tblStruct->name.c_str() << "/ hash" << tblStruct->hash;
    for (unsigned int i = 0; i < tblStruct->fields.size(); i++)
    {
    fieldStructure field = tblStruct->fields[i];
    LOG_INFO << "fieldName =" << field.name.c_str()
    << "/ fieldType =" << field.type.c_str()
    << "/ is key ? =" << field.isKey
    << "/ need Index ? =" << field.needIndex
    << "/ pos =" << field.pos
    << "/ arraySize =" << field.arraySize;
    }
    LOG_INFO << "----------------------------";
    */
    addTable(tblStruct);

    e = e.nextSiblingElement();
  }
  return true;
}

bool core::TableStructure::create()
{
  LOG_INFO << "Creating table" << name;
  QString create = "CREATE TABLE " + name + " (";

  std::list<QString> indexesToCreate;

  for (auto it = fields.begin(), itEnd = fields.end(); it != itEnd; ++it)
  {
    if ((*it)->arraySize == 1) // simple field
    {
      create += (*it)->name;
      create += " ";
      create += (*it)->type;

      if ((*it)->isKey)
        create += " PRIMARY KEY NOT NULL";

      create += ",";
    }
    else // complex field
    {
      for (unsigned int i = 1; i <= (*it)->arraySize; i++)
      {
        create += (*it)->name;
        create += QString::number(i);
        create += " ";
        create += (*it)->type;
        create += ",";
      }
    }

    if ((*it)->needIndex)
      indexesToCreate.push_back((*it)->name);
  }

  // remove spurious "," at the end of string, if any
  if (create.lastIndexOf(",") == create.length() - 1)
    create.remove(create.length() - 1, 1);
  create += ");";

  //LOG_INFO << create;

  sqlResult r = core::Game::instance().database().sqlQuery(create);

  if (r.valid)
  {
    LOG_INFO << "Table" << name << "successfully created";

    // create indexes
    for (auto it = indexesToCreate.begin(), itEnd = indexesToCreate.end(); it != itEnd; ++it)
    {
      QString query = QString("CREATE INDEX %1_%2 ON %1(%2)").arg(name).arg(*it);
      core::Game::instance().database().sqlQuery(query);
    }
  }

  return r.valid;
}

bool core::TableStructure::fill()
{
  LOG_INFO << "Filling table" << name << "...";

  DBFile * dbc = createDBFile();
  if (!dbc || !dbc->open())
    return false;

  QString query = "INSERT INTO ";
  query += name;
  query += "(";
  int nbFields = fields.size();
  int curfield = 0;
  for (auto it = fields.begin(), itEnd = fields.end();
    it != itEnd;
    ++it, curfield++)
  {
    if ((*it)->arraySize == 1) // simple field
    {
      query += (*it)->name;
    }
    else
    {
      for (unsigned int i = 1; i <= (*it)->arraySize; i++)
      {
        query += (*it)->name;
        query += QString::number(i);
        if (i != (*it)->arraySize)
          query += ",";
      }
    }
    if (curfield != nbFields - 1)
      query += ",";
  }

  query += ") VALUES";

  QString queryBase = query;
  int record = 0;
  int nbRecord = dbc->getRecordCount();
  if (nbRecord == 0)
  {
    // An empty table (Classic Era's Mount.db2 has no records): nothing to insert.
    LOG_INFO << "table" << name << "is empty";
    delete dbc;
    return true;
  }

  for (DBFile::Iterator it = dbc->begin(), itEnd = dbc->end(); it != itEnd; ++it, record++)
  {
    std::vector<std::string> Fields = it.get(this);

    for (int field = 0, nbfield = Fields.size(); field < nbfield; field++)
    {
      if (field == 0)
        query += " (";
      query += "\"";
      query += QString::fromStdString(Fields[field]);
      query += "\"";
      if (field != nbfield - 1)
        query += ",";
      else
        query += ")";
    }
    // inserting all records at once makes the application crash, so
    // insert in chunks of 200 lines. If it's the last record anyway
    // then don't, as the final query after the for() loop will do it:
    if (record % 200 == 0 && record != nbRecord - 1)
    {
      query += ";";
      GAMEDATABASE.reportTableFill((double)record / (double)nbRecord);
      sqlResult r = GAMEDATABASE.sqlQuery(query);
      if (!r.valid)
        return false;
      query = queryBase;
    }
    else
    {
      if (record != nbRecord - 1)
        query += ",";
    }
  }

  query += ";";
  sqlResult r = GAMEDATABASE.sqlQuery(query);

  if (r.valid)
    LOG_INFO << "table" << name << "successfuly filled";

  delete dbc;

  return r.valid;
}

DBFile * core::TableStructure::createDBFile()
{
  DBFile * result = 0;
  if (file.contains(".csv"))
    result = new CSVFile(file);

  return result;
}

core::TableStructure::~TableStructure()
{
  for (auto it : fields)
    delete it;
}
