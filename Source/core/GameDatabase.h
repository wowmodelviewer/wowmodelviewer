/*
 * GameDatabase.h
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#ifndef _GAMEDATABASE_H_
#define _GAMEDATABASE_H_

#include <functional>
#include <vector>
#include "sqlite3.h"

class DBFile;
class GameFile;

class QDomElement;
#include <QString>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _GAMEDATABASE_API_ __declspec(dllexport)
#    else
#        define _GAMEDATABASE_API_ __declspec(dllimport)
#    endif
#else
#    define _GAMEDATABASE_API_
#endif

class _GAMEDATABASE_API_ sqlResult
{
public:
  sqlResult() : valid(false), nbcols(0) {}
  ~sqlResult() { /* TODO :free char** */ }
  bool empty() { return values.size() == 0; }
  bool valid;
  int nbcols;
  std::vector<std::vector<QString> > values;
};

namespace core
{
  // table structures as defined in xml file
  class _GAMEDATABASE_API_ FieldStructure
  {
  public:
    FieldStructure() :
      name(""),
      type(""),
      isKey(false),
      needIndex(false),
    arraySize(1),
    id(0)
    {}

    virtual ~FieldStructure() {}

    QString name;
    QString type;
    bool isKey;
    bool needIndex;
    unsigned int arraySize;
    int id;
  };

  class _GAMEDATABASE_API_ TableStructure
  {
  public:
    TableStructure() :
      name(""),
      file("")
    {}

    virtual ~TableStructure();

    QString name;
    QString file;
    std::vector<FieldStructure *> fields;
    // Set by the game database's schema check (refreshStructures): this client's file of the table has a layout the
    // schema cannot be matched to (or is not installed), so it is created empty rather than read at wrong positions.
    bool skipFill = false;
    QString skipReason;

    bool create();
    bool fill();

    virtual DBFile * createDBFile();
  };


  class _GAMEDATABASE_API_ GameDatabase
  {
  public:
    GameDatabase();
    GameDatabase(GameDatabase &);

    bool initFromXML(const QString & file);

    sqlResult sqlQuery(const QString &query);

    void setFastMode() { m_fastMode = true; }
    // Closes the database (the application is ending): a database kept for this session only is deleted with it.
    void close() { closeDatabase(); }

    // Called while a database is built from its tables (never when a cached one is used): how far, 0..1 -- the
    // tables built so far plus the share of the one being filled.
    void setBuildProgressCallback(const std::function<void(float)> & cb) { m_buildProgress = cb; }
    // From a table being filled: the share of its records written so far.
    void reportTableFill(double share);

    virtual ~GameDatabase();

    void addTable(TableStructure *);

    virtual core::TableStructure * createTableStructure() = 0;
    virtual core::FieldStructure * createFieldStructure() = 0;

    virtual void readSpecificTableAttributes(QDomElement &, core::TableStructure *) = 0;
    virtual void readSpecificFieldAttributes(QDomElement &, core::FieldStructure *) = 0;

  protected:
    // Adjust table/field structures for the game build actually loaded, after
    // they have been read from the base XML (e.g. refresh DB2 field positions
    // from WoWDBDefs for a build newer than the shipped schema). Default no-op.
    virtual void refreshStructures(std::vector<TableStructure *> &) {}

    // Create secondary indexes on hot join/lookup columns, after all tables are populated.
    // Issued idempotently (CREATE INDEX IF NOT EXISTS) so an existing on-disk cache picks
    // them up on the next launch without a full rebuild. Default no-op.
    virtual void createIndices() {}

    // Whether the database just built holds everything its client will ever have: a table whose file is not on
    // this computer yet (a client still downloading) leaves the cache unstamped, so it is built again next time.
    virtual bool cacheComplete() const { return true; }

    // What else the tables are built from besides the schema folder (the game's table definitions): hashed into the
    // cache key, so a change to it builds the database again.
    virtual QByteArray cacheInputs() const { return QByteArray(); }
    // The state a build leaves besides the tables (the schema check), kept with a cached database and restored when
    // the database is used instead of built.
    virtual QByteArray saveCacheState() const { return QByteArray(); }
    virtual bool restoreCacheState(const QByteArray &) { return true; }

  private:
    static int treatQuery(void *NotUsed, int nbcols, char ** values, char ** cols);
    static void logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs);

    bool createDatabaseFromXML(const QString & file);
    bool readStructureFromXML(const QString & file);
    // Everything that identifies this client's database (see initFromXML); empty when the build is unknown.
    QString cacheKey() const;
    bool openDatabase(const QString & path, bool building);
    void closeDatabase();

    sqlite3 *m_db;
    QString m_sessionFile;   // a database built for this session only, deleted when it is closed
    std::function<void(float)> m_buildProgress;
    size_t m_buildTable = 0, m_buildTables = 0;
    int m_failedTables = 0;  // tables of the build that could not be filled (the build is then not kept)

    std::vector<TableStructure * > m_dbStruct;

    bool m_fastMode;
  };

}


#endif /* _WOWDATABASE_H_ */
