/*
 * GameDatabase.h
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#ifndef _GAMEDATABASE_H_
#define _GAMEDATABASE_H_

#include <map>
#include <vector>
#include "sqlite3.h"

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
#        define _GAMEDATABASE_API_ __declspec(dllexport)
#    else
#        define _GAMEDATABASE_API_ __declspec(dllimport)
#    endif
#else
#    define _GAMEDATABASE_API_
#endif

class _GAMEDATABASE_API_ GameDatabase
{
  public:
    GameDatabase();
    GameDatabase(GameDatabase &);

    bool initFromXML(const QString & file);

    sqlResult sqlQuery(const QString &query);

    void setFastMode() { m_fastMode = true; }

    ~GameDatabase();

    // table structures as defined in xml file
    class fieldStructure
    {
      public:
        fieldStructure() :
          name(""),
          type(""),
          isKey(false),
          pos(-1),
          arraySize(0),
          id(0)
        {}

        QString name;
        QString type;
        bool isKey;
        int pos;
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

    sqlite3 *m_db;

    std::vector<tableStructure> m_dbStruct;

    bool m_fastMode;
};



#endif /* _GAMEDATABASE_H_ */
