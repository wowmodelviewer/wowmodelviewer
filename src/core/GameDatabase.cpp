/*
 * GameDatabase.cpp
 *
 *  Created on: 7 Aug. 2017
 *      Author: Jeromnimo
 */

#include "GameDatabase.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QFile>

#include "logger/Logger.h"



core::GameDatabase::~GameDatabase()
{
  if(m_db)
    sqlite3_close(m_db);
}


core::GameDatabase::GameDatabase()
: m_db(NULL), m_fastMode(false)
{

}

bool core::GameDatabase::initFromXML(const QString & file)
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

sqlResult core::GameDatabase::sqlQuery(const QString & query)
{
  sqlResult result;

  char *zErrMsg = 0;
  int rc = sqlite3_exec(m_db, query.toStdString().c_str(), core::GameDatabase::treatQuery, (void *)&result, &zErrMsg);
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

void core::GameDatabase::addTable(TableStructure * tbl)
{
  m_dbStruct.push_back(tbl);
}

int core::GameDatabase::treatQuery(void *resultPtr, int nbcols, char ** vals , char ** cols)
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

bool core::GameDatabase::createDatabaseFromXML(const QString & file)
{
  if (!readStructureFromXML(file))
  {
    LOG_ERROR << "Reading database structure from XML file failed ! Impossible to create database.";
    return false;
  }

  bool result = true; // ok until we found an issue

  for (auto it = m_dbStruct.begin(), itEnd = m_dbStruct.end(); it != itEnd; ++it)
  {
    if ((*it)->create())
    {
      if (!(*it)->fill())
      {
        LOG_ERROR << "Error during table filling" << (*it)->name;
        result = false;
      }
    }
    else
    {
      LOG_ERROR << "Error during table creation" << (*it)->name;
      result = false;
    }
  }

  return result; 
}

void core::GameDatabase::logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs)
{
  if(aTimeInNs/1000000 > 30)
  {
    LOG_ERROR << "LONG QUERY !";
    LOG_ERROR << aQueryStr;
    LOG_ERROR << "Query time (ms)" << aTimeInNs/1000000;
  }

}

bool core::GameDatabase::readStructureFromXML(const QString & file)
{
  QDomDocument doc;

  QFile f(file);
  f.open(QIODevice::ReadOnly);
  doc.setContent(&f);
  f.close();

  QDomElement docElem = doc.documentElement();

  QDomElement e = docElem.firstChildElement();

  while (!e.isNull())
  {
    core::TableStructure * tblStruct = createTableStructure();
    QDomElement child = e.firstChildElement();

    QDomNamedNodeMap attributes = e.attributes();
    QDomNode hash = attributes.namedItem("layoutHash");
    QDomNode gamefile = attributes.namedItem("gamefile");

    // table values
    tblStruct->name = attributes.namedItem("name").nodeValue();
    if (!hash.isNull())
      tblStruct->hash = hash.nodeValue().toUInt();

    if (!gamefile.isNull())
      tblStruct->file = gamefile.nodeValue();
    else
      tblStruct->file = tblStruct->name;

    int fieldId = 0;
    while (!child.isNull())
    {
      core::FieldStructure * fieldStruct = createFieldStructure();
      fieldStruct->id = fieldId;
      QDomNamedNodeMap attributes = child.attributes();

      // search if name and type are here
      QDomNode name = attributes.namedItem("name");
      QDomNode type = attributes.namedItem("type");
      QDomNode key = attributes.namedItem("primary");
      QDomNode arraySize = attributes.namedItem("arraySize");
      QDomNode pos = attributes.namedItem("pos");
      QDomNode commonData = attributes.namedItem("commonData");
      QDomNode index = attributes.namedItem("createIndex");

      if (!name.isNull() && !type.isNull())
      {
        fieldStruct->name = name.nodeValue();
        fieldStruct->type = type.nodeValue();

        if (!key.isNull())
          fieldStruct->isKey = true;

        if (!index.isNull())
          fieldStruct->needIndex = true;

        if (!pos.isNull())
          fieldStruct->pos = pos.nodeValue().toInt();

        if (!commonData.isNull())
          fieldStruct->isCommonData = true;

        if (!arraySize.isNull())
          fieldStruct->arraySize = arraySize.nodeValue().toUInt();

        tblStruct->fields.push_back(*fieldStruct);
      }

      fieldId++;
      child = child.nextSiblingElement();
    }

    /*
    LOG_INFO << "----------------------------";
    LOG_INFO << "Table" << tblStruct->name.c_str() << "/ hash" << tblStruct->hash;
    for (unsigned int i = 0; i < tblStruct->fields.size(); i++)
    {
    fieldStructure field = tblStruct->fields[i];
    LOG_INFO << "fieldName =" << field.name.c_str()
    << "/ fieldType =" << field.type.c_str()
    << "/ is key ? =" << field.isKey
    << "/ need Index ? =" << field.needIndex
    << "/ pos =" << field.pos
    << "/ arraySize =" << field.arraySize;
    }
    LOG_INFO << "----------------------------";
    */
    addTable(tblStruct);

    e = e.nextSiblingElement();
  }
  return true;
}

bool core::TableStructure::create()
{
  LOG_INFO << "Creating table" << name;
  QString create = "CREATE TABLE " + name + " (";

  std::list<QString> indexesToCreate;

  for (auto it = fields.begin(), itEnd = fields.end(); it != itEnd; ++it)
  {
    if (it->arraySize == 1) // simple field
    {
      create += it->name;
      create += " ";
      create += it->type;

      if (it->isKey)
        create += " PRIMARY KEY NOT NULL";

      create += ",";
    }
    else // complex field
    {
      for (unsigned int i = 1; i <= it->arraySize; i++)
      {
        create += it->name;
        create += QString::number(i);
        create += " ";
        create += it->type;
        create += ",";
      }
    }

    if (it->needIndex)
      indexesToCreate.push_back(it->name);
  }

  // remove spurious "," at the end of string, if any
  if (create.lastIndexOf(",") == create.length() - 1)
    create.remove(create.length() - 1, 1);
  create += ");";

  //LOG_INFO << create;

  sqlResult r = core::Game::instance().database().sqlQuery(create);

  if (r.valid)
  {
    LOG_INFO << "Table" << name << "successfully created";

    // create indexes
    for (auto it = indexesToCreate.begin(), itEnd = indexesToCreate.end(); it != itEnd; ++it)
    {
      QString query = QString("CREATE INDEX %1_%2 ON %1(%2)").arg(name).arg(*it);
      core::Game::instance().database().sqlQuery(query);
    }
  }

  return r.valid;
}