/*
 * GameDatabase.h
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#ifndef _GAMEDATABASE_H_
#define _GAMEDATABASE_H_

#include <map>
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

  class _GAMEDATABASE_API_ TableStructure
  {
  public:
    TableStructure() :
      name(""),
      gamefile(""),
      hash(0)
    {}

    QString name;
    QString gamefile;
    unsigned int hash;
    std::vector<FieldStructure> fields;

    virtual bool create() = 0;
    virtual bool fill() = 0;
  };


  class _GAMEDATABASE_API_ GameDatabase
  {
  public:
    GameDatabase();
    GameDatabase(GameDatabase &);

    bool initFromXML(const QString & file);

    sqlResult sqlQuery(const QString &query);

    void setFastMode() { m_fastMode = true; }

    virtual ~GameDatabase();

    void addTable(TableStructure *);

  protected:
    virtual bool readStructureFromXML(const QString & file) = 0;

  private:
    static int treatQuery(void *NotUsed, int nbcols, char ** values, char ** cols);
    static void logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs);

    bool createDatabaseFromXML(const QString & file);

    sqlite3 *m_db;

    std::vector<TableStructure * > m_dbStruct;

    bool m_fastMode;
  };

}


#endif /* _WOWDATABASE_H_ */
