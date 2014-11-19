/*
 * GameDatabase.h
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#ifndef _GAMEDATABASE_H_
#define _GAMEDATABASE_H_

#include "sqlite3.h"

#include <map>
#include <string>
#include <vector>

class QDomElement;

#define GAMEDATABASE GameDatabase::instance()

class sqlResult
{
  public:
    sqlResult() : valid(false), nbcols(0) {}
    ~sqlResult() { /* TODO :free char** */ }
    bool empty() { return values.size() == 0; }
    bool valid;
    int nbcols;
    std::vector<std::vector<std::string> > values;
};

class GameDatabase
{
  public:
    static GameDatabase & instance()
    {
      static GameDatabase m_instance;
      return m_instance;
    }

    bool initFromXML(const std::string & file);

    sqlResult sqlQuery(const std::string &query);

    ~GameDatabase();

  private:
    GameDatabase();
    GameDatabase(GameDatabase &);

    static int treatQuery(void *NotUsed, int nbcols, char ** values , char ** cols);

    bool createDatabaseFromXML(const std::string & file);
    bool createTableFromXML(const QDomElement &);
    bool fillTableFromGameFile(const std::string & table, const std::string & gamefile);

    sqlite3 *m_db;
    // std::map<TableName, [fieldID] <fieldName,fieldType> >
    // ie m_dbStruct["FileData"][0] => pair<"id","unit">
    std::map<std::string, std::map<int, std::pair<std::string, std::string> > >  m_dbStruct;

};



#endif /* _GAMEDATABASE_H_ */
