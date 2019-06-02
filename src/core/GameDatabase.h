/*
 * GameDatabase.h
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#ifndef _GAMEDATABASE_H_
#define _GAMEDATABASE_H_

#include <vector>
#include "sqlite3.h"
#include <map>

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
  ~sqlResult() = default;
  bool empty() const { return values.empty(); }
  bool valid;
  int nbcols;
  std::vector<std::vector<QString> > values;
};

class _GAMEDATABASE_API_ sqlResultAssoc
{
public:
  sqlResultAssoc() : valid(false) {}
  ~sqlResultAssoc() = default;
  bool empty() const { return values.empty(); }
  bool valid;
  std::vector<std::map<QString, QString> > values;
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

    sqlResult sqlQuery(const QString &query) const;
    sqlResultAssoc sqlQueryAssoc(const QString &query) const;

    void setFastMode() { m_fastMode = true; }

    virtual ~GameDatabase();

    void addTable(TableStructure *);

    virtual core::TableStructure * createTableStructure() = 0;
    virtual core::FieldStructure * createFieldStructure() = 0;

    virtual void readSpecificTableAttributes(QDomElement &, core::TableStructure *) = 0;
    virtual void readSpecificFieldAttributes(QDomElement &, core::FieldStructure *) = 0;

  protected:

  private:
    static int treatQuery(void *resultPtr, int nbcols, char ** vals, char ** cols);
    static int treatQueryAssoc(void *resultPtr, int nbcols, char ** vals, char ** cols);

    static void logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs);

    bool createDatabaseFromXML(const QString & file);
    bool readStructureFromXML(const QString & file);

    sqlite3 *m_db;

    std::vector<TableStructure * > m_dbStruct;

    bool m_fastMode;
  };

}


#endif /* _WOWDATABASE_H_ */
