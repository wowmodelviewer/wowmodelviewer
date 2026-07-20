/*
 * GameDatabase.cpp
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#include "GameDatabase.h"

#include "dbfile.h"
#include "CSVFile.h"

#include <QDomElement>
#include <QFile>

#include <exception>

#include "logger/Logger.h"
#include "Game.h"

core::GameDatabase::~GameDatabase()
{
  if(m_db)
    sqlite3_close(m_db);
}


core::GameDatabase::GameDatabase()
: m_db(NULL), m_fastMode(true) // cache the built DB to disk and reuse it across launches (see initFromXML)
{

}

bool core::GameDatabase::initFromXML(const QString & file)
{
   int rc = 1;

   // Fast mode keeps a persistent on-disk copy of the database (wowdb.sqlite) so
   // we can skip the ~20s DB2 -> SQLite rebuild on every launch. createDatabaseFromXML
   // only fill()s a table when create() succeeds, and create() ("CREATE TABLE")
   // fails for tables that already exist -- so an existing cache is reused as-is.
   // We MUST invalidate the cache when the WoW build changes, otherwise we'd serve
   // stale/mismatched table data, so the build version is recorded alongside it.
   static const char * DB_PATH  = "./wowdb.sqlite";
   static const char * VER_PATH = "./wowdb.sqlite.build";
   QString buildVersion;

   if(m_fastMode)
   {
     // Cache key = WoW build + our schema version. Bump SCHEMA_VERSION whenever the
     // table layout in database.xml (or how we read it) changes, so an old cache
     // built with a different schema is rebuilt rather than queried and failing.
     static const int SCHEMA_VERSION = 13; // 13: Voice Lines V3 "Encounter Dialogue" adds JournalEncounter + JournalEncounterCreature (CreatureDisplayInfo -> encounter name -> VO folder, for raid bosses whose VO folder is named after the boss and not the model). 12: Voice Lines V1 adds sound tables (CreatureSoundData/SoundKit/SoundKitEntry/SoundKitName) + CreatureModelData.SoundID + CreatureDisplayInfo.SoundID/NPCSoundID. 11: ChrCustomizationReq adds ReqAchievementID/ReqQuestID/ReqItemModifiedAppearanceID (unlock-gate filter for customization choices). 10: corrected ItemSparse name-field positions (sparse-record string walk) for 12.0.7. 9: ChrCustomizationReq/ChrRaces/CreatureDisplayInfo/CreatureModelData. Bump forces a cache rebuild so the fix reaches installs upgraded over a prior build
     const QString build = GAMEDIRECTORY.version(); // current WoW build, e.g. "12.0.1.66220"
     buildVersion = build.isEmpty() ? QString() : (build + "|schema" + QString::number(SCHEMA_VERSION));

     QString cachedVersion;
     {
       QFile vf(VER_PATH);
       if (vf.open(QIODevice::ReadOnly | QIODevice::Text))
       {
         cachedVersion = QString::fromUtf8(vf.readAll()).trimmed();
         vf.close();
       }
     }

     if (buildVersion.isEmpty() || cachedVersion != buildVersion)
     {
       LOG_INFO << "Database cache stale or missing (cached:" << cachedVersion
                << "/ current:" << buildVersion << ") - rebuilding from DB2";
       QFile::remove(DB_PATH);
       QFile::remove(VER_PATH);
     }
     else
     {
       LOG_INFO << "Reusing cached database for" << buildVersion << "(skipping DB2 rebuild)";
     }

     rc = sqlite3_open(DB_PATH, &m_db);
   }
   else
    rc = sqlite3_open(":memory:", &m_db);

   if( rc )
   {
     LOG_INFO << "Can't open database:" << sqlite3_errmsg(m_db);
     return false;
   }
   else
   {
     LOG_INFO << "Opened database successfully";
   }

   if (m_fastMode)
   {
     // The build does thousands of chunked INSERTs; with the default rollback
     // journal + synchronous=FULL each commit fsyncs, which would make the first
     // on-disk build far slower than the in-memory one. The cache is always
     // rebuildable (invalidated by build version) so durability is irrelevant.
     sqlite3_exec(m_db, "PRAGMA synchronous=OFF; PRAGMA journal_mode=MEMORY; PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
   }

   // Performance: memory-map the on-disk cache and give SQLite a big page cache so
   // the many small lookups each action fires (customization choices, item/equipment
   // display info, texture/section composition) are served from RAM instead of disk
   // reads. The DB is ~80 MB, so a 512 MB mmap window covers it entirely.
   sqlite3_exec(m_db,
     "PRAGMA mmap_size=536870912;"   // 512 MB memory-mapped I/O
     "PRAGMA cache_size=-131072;",   // 128 MB page cache (negative = KiB)
     nullptr, nullptr, nullptr);

   sqlite3_profile(m_db, GameDatabase::logQueryTime, m_db);

   const bool ok = createDatabaseFromXML(core::Game::instance().configFolder() + file);

   // Record the build the cache was produced for, so it can be validated next time.
   // (Skip when the build version is unknown -- we don't want to trust a blind cache.)
   if (m_fastMode && ok && !buildVersion.isEmpty())
   {
     QFile vf(VER_PATH);
     if (vf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
     {
       vf.write(buildVersion.toUtf8());
       vf.close();
     }
   }

   return ok;
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
  if (!readStructureFromXML(file))
  {
    LOG_ERROR << "Reading database structure from XML file failed ! Impossible to create database.";
    return false;
  }

  // Let the game-specific database adapt the structures to the loaded build
  // (e.g. refresh DB2 field positions from WoWDBDefs).
  refreshStructures(m_dbStruct);

  bool result = true; // ok until we found an issue

  for (auto it = m_dbStruct.begin(), itEnd = m_dbStruct.end(); it != itEnd; ++it)
  {
    // Build each table defensively: a single table that fails (e.g. an
    // unsupported layout on a newer build, or a bad alloc on a very large
    // sparse table) must not abort the whole database or crash the app -- the
    // other tables (and the model the user wants) can still load.
    try
    {
      if ((*it)->create())
      {
        if (!(*it)->fill() && !m_fastMode)
        {
          LOG_ERROR << "Error during table filling" << (*it)->name;
          result = false;
        }
      }
      else
      {
        if (!m_fastMode) // if table already exists in fast mode, continue
        {
          LOG_ERROR << "Error during table creation" << (*it)->name;
          result = false;
        }
      }
    }
    catch (const std::exception & e)
    {
      LOG_ERROR << "Exception while building table" << (*it)->name << ":" << e.what() << "- skipping table";
    }
    catch (...)
    {
      LOG_ERROR << "Unknown exception while building table" << (*it)->name << "- skipping table";
    }
  }

  for (auto it : m_dbStruct)
    delete it;

  // All tables are populated (or reused from cache) -- create the secondary indexes on
  // the hot join/lookup columns. Idempotent, so this is cheap on an already-indexed cache.
  createIndices();

  return result;
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

    readSpecificTableAttributes(child, tblStruct);

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
