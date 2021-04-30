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

namespace core
{
  // table structures as defined in xml file
  class FieldStructure
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

  class TableStructure
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


  class GameDatabase
  {
  public:
    GameDatabase();
    GameDatabase(GameDatabase &);

    bool initFromXML(const QString & file);

    sqlResult sqlQuery(const QString &query);

    void setFastMode() { m_fastMode = true; }

    virtual ~GameDatabase();

    void addTable(TableStructure *);

    virtual core::TableStructure * createTableStructure() = 0;
    virtual core::FieldStructure * createFieldStructure() = 0;

    virtual void readSpecificTableAttributes(QDomElement &, core::TableStructure *) = 0;
    virtual void readSpecificFieldAttributes(QDomElement &, core::FieldStructure *) = 0;

  protected:

  private:
    static int treatQuery(void *NotUsed, int nbcols, char ** values, char ** cols);
    static void logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs);

    bool createDatabaseFromXML(const QString & file);
    bool readStructureFromXML(const QString & file);

    sqlite3 *m_db;

    std::vector<TableStructure * > m_dbStruct;

    bool m_fastMode;
  };

}


#endif /* _WOWDATABASE_H_ */
