/*
 * WoWDatabase.h
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#ifndef _WOWDATABASE_H_
#define _WOWDATABASE_H_

#include "GameDatabase.h"

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
        int notInstalled = 0;  // the table's file is not on this computer (or not in this client)
        int absentFields = 0;  // fields the client's layout does not have (read as empty)
        QStringList notes;     // one line per table that was not verified
      };
      const SchemaCheck & schemaCheck() const { return m_schemaCheck; }

    protected:
      // Refresh each table's DB2 field positions from its WoWDBDefs (.dbd)
      // definition for the loaded build, so the curated XML names/types stay
      // but positions track the current game version.
      void refreshStructures(std::vector<core::TableStructure *> &) override;
      bool cacheComplete() const override { return m_schemaCheck.notInstalled == 0; }

      // Secondary indexes on the hot foreign-key / join columns the per-load
      // customization/equipment/creature queries filter on (database.xml declares
      // only PRIMARY KEYs, so these were full table scans of 30k-220k-row tables).
      void createIndices() override;

    private:
      SchemaCheck m_schemaCheck;
  };

}

#endif /* _WOWDATABASE_H_ */
