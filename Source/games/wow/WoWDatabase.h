/*
 * WoWDatabase.h
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#ifndef _WOWDATABASE_H_
#define _WOWDATABASE_H_

#include "GameDatabase.h"

#include <QMap>
#include <QStringList>

class DBFile;
class GameFile;

class QDomElement;


#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WOWDATABASE_API_ __declspec(dllexport)
#    else
#        define _WOWDATABASE_API_ __declspec(dllimport)
#    endif
#else
#    define _WOWDATABASE_API_
#endif

namespace wow
{
  class TableStructure : public core::TableStructure
  {
  public:
    TableStructure() :
      core::TableStructure(), hash(0)
    {
    }

    unsigned int hash;
    // database.xml verifiedLayoutOnly="yes": read only from a definition of the file's own layout hash (dbd LAYOUT), never
    // at the schema's positions, from its layoutHash or from a BUILD match -- a table whose wrong reading could not be
    // noticed downstream.
    bool verifiedLayoutOnly = false;

    DBFile * createDBFile();

  };

  class FieldStructure : public core::FieldStructure
  {
  public:
    FieldStructure() :
      core::FieldStructure(), pos(-1), isCommonData(false), isRelationshipData(false)
    {
    }

    int pos;
    bool isCommonData;
    bool isRelationshipData;
    // What the column reads as when the client's layout does not have it (database.xml absentValue; default "0",
    // or empty text). For a mask, "no restriction" is the safe reading, not "nothing allowed".
    std::string absentValue;
  };

  class _WOWDATABASE_API_ WoWDatabase : public core::GameDatabase
  {
    public:
      WoWDatabase();
      WoWDatabase(WoWDatabase &);

      ~WoWDatabase() {}

      core::TableStructure * createTableStructure();
      core::FieldStructure * createFieldStructure();

      void readSpecificTableAttributes(QDomElement &, core::TableStructure *);
      void readSpecificFieldAttributes(QDomElement &, core::FieldStructure *);

      // THE SCHEMA CHECK of the last load (refreshStructures): how each table's columns were placed for this client.
      struct SchemaCheck
      {
        int verified = 0;      // a WoWDBDefs definition matched this client's own file (layout hash or build)
        int trusted = 0;       // no definition matched, kept at the schema's positions: a client of its generation
        int notRead = 0;       // no definition matched, and the client is of another generation: not read
        int notInstalled = 0;  // the table's file is listed for this build but not on this computer (yet)
        int notInClient = 0;   // the table's file is not in this client at all (or not a DB2 table)
        int absentFields = 0;  // fields the client's layout does not have (read as empty)
        QStringList notes;     // one line per table that was not verified

        // Per DB2 table: how this client's file of it was read, for code that must tell "no row" from "not read".
        struct TableReading
        {
          enum class State { Verified, VerifiedBySchemaLayout, KeptAtSchemaPositions, NotRead, NotInstalled, NotInClient };
          State state = State::NotRead;
          QString layout;            // the file's layout hash (8 hex digits); empty when no file was found
          QStringList absentFields;  // columns this client's layout does not have (each reads as its absentValue)
          QString reason;            // why the table was not read (NotRead / NotInstalled / NotInClient)
          qint64 rows = -1;          // records written to the table by the build (-1: not filled)
          qint64 lockedRows = 0;     // records in sections encrypted with a key this computer lacks (not read)
          QStringList missingKeys;   // those keys' names (16 hex digits): one this computer gains makes them readable
        };
        QMap<QString, TableReading> tables;
        QString build, product;      // the client this check is of (empty: no client's database read yet)
      };
      const SchemaCheck & schemaCheck() const { return m_schemaCheck; }
      // The layout hash in the loaded client's own file of a table (8 hex digits), read from the file's header now;
      // empty when the file is not there or not a WDC table.
      static QString fileLayoutHash(const QString & tableFile);
      // How the table was read for this client; null when the schema has no such DB2 table (or no client is loaded).
      const SchemaCheck::TableReading * tableReading(const QString & table) const
      {
        auto it = m_schemaCheck.tables.constFind(table);
        return it == m_schemaCheck.tables.constEnd() ? nullptr : &it.value();
      }

    protected:
      // Refresh each table's DB2 field positions from its WoWDBDefs (.dbd)
      // definition for the loaded build, so the curated XML names/types stay
      // but positions track the current game version.
      void refreshStructures(std::vector<core::TableStructure *> &) override;
      bool cacheComplete() const override { return m_schemaCheck.notInstalled == 0; }
      // A client's database is being read: the previous client's check goes, even if this one then fails to open.
      void clientDatabaseStarting() override { m_schemaCheck = SchemaCheck(); }
      // Keeps the rows read and the rows in encrypted sections of each table in its TableReading.
      void tableBuilt(const core::TableStructure & table) override;
      // The game's table definitions (dbd/*.dbd) the field positions come from.
      QByteArray cacheInputs() const override;
      // The schema check, kept with a cached database (Model > Info and the client's capabilities read it).
      QByteArray saveCacheState() const override;
      bool restoreCacheState(const QByteArray & state) override;

      // Secondary indexes on the hot foreign-key / join columns the per-load
      // customization/equipment/creature queries filter on (database.xml declares
      // only PRIMARY KEYs, so these were full table scans of 30k-220k-row tables).
      void createIndices() override;

    private:
      SchemaCheck m_schemaCheck;
  };

}

#endif /* _WOWDATABASE_H_ */
