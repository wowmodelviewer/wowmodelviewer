/*
 * GameDatabase.cpp
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#include "GameDatabase.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QFile>

#include "dbcfile.h"
#include "Game.h"
#include "logger/Logger.h"

GameDatabase * GameDatabase::m_instance = 0;

const std::vector<QString> POSSIBLE_DB_EXT = {".dbc", ".db2"};

GameDatabase::~GameDatabase()
{
  if(m_db)
    sqlite3_close(m_db);
}


GameDatabase::GameDatabase()
: m_db(NULL), m_fastMode(false)
{

}

bool GameDatabase::initFromXML(const QString & file)
{
   int rc = 1;

   if(m_fastMode)
	  rc = sqlite3_open("./wowdb.sqlite", &m_db);
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

   sqlite3_profile(m_db, GameDatabase::logQueryTime, m_db);
   return createDatabaseFromXML(file);
}

sqlResult GameDatabase::sqlQuery(const QString & query)
{
  sqlResult result;

  char *zErrMsg = 0;
  int rc = sqlite3_exec(m_db, query.toStdString().c_str(), GameDatabase::treatQuery, (void *)&result, &zErrMsg);
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

int GameDatabase::treatQuery(void *resultPtr, int nbcols, char ** vals , char ** cols)
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

bool GameDatabase::createDatabaseFromXML(const QString & file)
{
  QDomDocument doc;

  QFile f(file);
  f.open(QIODevice::ReadOnly);
  doc.setContent(&f);
  f.close();

  QDomElement docElem = doc.documentElement();

  QDomElement e = docElem.firstChildElement();

  while(!e.isNull())
  {
    if(!createTableFromXML(e))
    {
      LOG_ERROR << "Impossible to create table" << e.tagName();
    }
    else
    {
      LOG_INFO << "Table creation succeed for" << e.tagName();
      QString gamedbfile = e.firstChildElement("gamefile").attributes().namedItem("name").nodeValue();
      if(gamedbfile != "")
      {
        LOG_INFO << "Opening game database file" << gamedbfile;
        if(!fillTableFromGameFile(e.tagName(), gamedbfile))
        {
          LOG_ERROR << "Error during filling of database" << e.tagName();
          return false;
        }
      }
      else
      {
        LOG_ERROR << "Fail to find gamefile attribute";
        return false;
      }
    }

    e = e.nextSiblingElement();
  }
  return true;
}

bool GameDatabase::createTableFromXML(const QDomElement & elem)
{
  QDomElement child = elem.firstChildElement();
  QDomElement lastChild = elem.lastChildElement();

  QString curTable = elem.nodeName();
  QString create = "CREATE TABLE "+ curTable +" (";

  int curfield = 0;
  while (!child.isNull())
  {
    QDomNamedNodeMap attributes = child.attributes();

    // search if name and type are here
    QDomNode name = attributes.namedItem("name");
    QDomNode type = attributes.namedItem("type");
    QDomNode index = attributes.namedItem("index");

    if(!name.isNull() && !type.isNull())
    {
      int fieldIndex;
      if(!index.isNull())
        fieldIndex = index.nodeValue().toInt();
      else
        fieldIndex = curfield++;

      QString fieldName = name.nodeValue();
      QString fieldType = type.nodeValue();

     //LOG_INFO << "fieldName = " << fieldName << " / fieldType = " << fieldType << " / fieldIndex = " << fieldIndex << std::endl;

      create += fieldName;
      create += " ";
      create += fieldType;
      if(!attributes.namedItem("primary").isNull())
        create += " PRIMARY KEY NOT NULL";
      create += ",";
      m_dbStruct[curTable][fieldIndex] = std::make_pair(fieldName,fieldType);
    }

    child = child.nextSiblingElement();
  }

  // remove spurious "," at the end of string, if any
  if(create.lastIndexOf(",") == create.length()-1)
    create.remove(create.length()-1,1);
  create += ");";

  //std::cout << create << std::endl;

  sqlResult r = sqlQuery(create);

  // add indexes to speed up item loading
  if(curTable == "TextureFileData")
  {
    sqlQuery("CREATE INDEX index1 ON TextureFileData(TextureItemID)");
    sqlQuery("CREATE INDEX index2 ON TextureFileData(TextureType)");
    sqlQuery("CREATE INDEX index3 ON TextureFileData(FileDataID)");
  }


  return r.valid;
}

bool GameDatabase::fillTableFromGameFile(const QString & table, const QString & gamefile)
{
  // loop over possible extension to check if file exists
  QString fileToOpen = "";
  for(unsigned int i=0 ; i < POSSIBLE_DB_EXT.size() ; i++)
  {
    QString filename = gamefile+POSSIBLE_DB_EXT[i];
    if(GAMEDIRECTORY.fileExists(filename.toStdString()))
    {
      fileToOpen = filename;
      break;
    }
  }

  if(fileToOpen.isEmpty())
    return false;

  DBCFile dbc(fileToOpen.toStdString().c_str());
  if(!dbc.open())
    return false;

  std::map<int, std::pair<QString, QString> > tableStruct = m_dbStruct[table];

  QString query = "INSERT INTO ";
  query += table;
  query += "(";
  int nbFields = tableStruct.size();
  //std::cout << __FUNCTION__ << " nb fields = " << nbFields << std::endl;
  int curfield = 0;
  for(std::map<int, std::pair<QString, QString> >::iterator it = tableStruct.begin(), itEnd = tableStruct.end();
      it != itEnd ;
      ++it,curfield++)
  {
    query += it->second.first;
    if(curfield != nbFields-1)
      query += ",";
  }

  query += ") VALUES";
  QString queryBase = query;
  int record = 0;
  int nbRecord = dbc.getRecordCount();
 // std::cout << "nb fields (from dbc) : " << dbc.getFieldCount() << std::endl;
  for(DBCFile::Iterator it = dbc.begin(), itEnd = dbc.end(); it != itEnd; ++it,record++)
  {
    std::vector<std::string> fields = it->get(tableStruct);
    for(int field=0 , nbfield = fields.size(); field < nbfield ; field++)
    {
      if(field == 0)
        query += " (";
      query += "\"";
      query += QString::fromStdString(fields[field]);
      query += "\"";
      if(field != nbfield-1)
        query += ",";
      else
        query += ")";
    }
    // inserting all items at once make application crash,
    // so insert by chunk of 200 lines
    if(record%200 == 0)
    {
      query += ";";
      sqlResult r = sqlQuery(query);
      if(!r.valid)
        return false;
      query = queryBase;
    }
    else
    {
      if(record != nbRecord-1)
        query+=",";
    }
  }

  query += ";";
  sqlResult r = sqlQuery(query);
  return r.valid;
}

void GameDatabase::logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs)
{
  if(aTimeInNs/1000000 > 30)
  {
    LOG_ERROR << "LONG QUERY !";
    LOG_ERROR << aQueryStr;
    LOG_ERROR << "Query time (ms)" << aTimeInNs/1000000;
  }

}
