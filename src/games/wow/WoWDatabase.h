/*
 * WoWDatabase.h
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#ifndef _WOWDATABASE_H_
#define _WOWDATABASE_H_

#include <map>
#include <vector>
#include "sqlite3.h"

class DBFile;
class GameFile;

class QDomElement;
#include <QString>

class sqlResult
{
  public:
    sqlResult() : valid(false), nbcols(0) {}
    ~sqlResult() { /* TODO :free char** */ }
    bool empty() { return values.size() == 0; }
    bool valid;
    int nbcols;
    std::vector<std::vector<QString> > values;
};

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WOWDATABASE_API_ __declspec(dllexport)
#    else
#        define _WOWDATABASE_API_ __declspec(dllimport)
#    endif
#else
#    define _WOWDATABASE_API_
#endif

class _WOWDATABASE_API_ WoWDatabase
{
  public:
    WoWDatabase();
    WoWDatabase(WoWDatabase &);

    bool initFromXML(const QString & file);

    sqlResult sqlQuery(const QString &query);

    void setFastMode() { m_fastMode = true; }

    ~WoWDatabase();

    // table structures as defined in xml file
    class fieldStructure
    {
      public:
        fieldStructure() :
          name(""),
          type(""),
          isKey(false),
          pos(-1),
          arraySize(1),
          id(0),
          needIndex(false),
          isCommonData(false)
        {}

        QString name;
        QString type;
        bool isKey;
        bool needIndex;
        int pos;
        bool isCommonData;
        unsigned int arraySize;
        int id;
    };

    class tableStructure
    {
      public:
        tableStructure() :
          name(""),
          gamefile(""),
          hash(0)
        {}

        QString name;
        QString gamefile;
        unsigned int hash;
        std::vector<fieldStructure> fields;

        bool create();
        bool fill();
    };


  private:
    static int treatQuery(void *NotUsed, int nbcols, char ** values , char ** cols);
    static void logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs);

    bool readStructureFromXML(const QString & file);
    bool createDatabaseFromXML(const QString & file);


    static DBFile * createDBFile(GameFile *);

    sqlite3 *m_db;

    std::vector<tableStructure> m_dbStruct;

    bool m_fastMode;
};



#endif /* _WOWDATABASE_H_ */
