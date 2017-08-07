/*
 * WoWDatabase.cpp
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#include "WoWDatabase.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QFile>

#include "wdb2file.h"
#include "wdb5file.h"
#include "wdb6file.h"
#include "Game.h"
#include "logger/Logger.h"

const std::vector<QString> POSSIBLE_DB_EXT = {".db2", ".dbc"};

wow::WoWDatabase::WoWDatabase()
  : GameDatabase()
{

}

bool wow::WoWDatabase::readStructureFromXML(const QString & file)
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
    wow::TableStructure * tblStruct = new wow::TableStructure();
    QDomElement child = e.firstChildElement();

    QDomNamedNodeMap attributes = e.attributes();
    QDomNode hash = attributes.namedItem("layoutHash");
    QDomNode gamefile = attributes.namedItem("gamefile");

    // table values
    tblStruct->name = attributes.namedItem("name").nodeValue();
    if (!hash.isNull())
      tblStruct->hash = hash.nodeValue().toUInt();

    if (!gamefile.isNull())
      tblStruct->gamefile = gamefile.nodeValue();
    else
      tblStruct->gamefile = tblStruct->name;

    int fieldId = 0;
    while (!child.isNull())
    {
      core::FieldStructure * fieldStruct = new core::FieldStructure();
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

DBFile * wow::WoWDatabase::createDBFile(GameFile * fileToOpen)
{
  DBFile * result = 0;
  if (fileToOpen)
  {
    if (fileToOpen->open())
    {
      char header[5];
      
      fileToOpen->read(header, 4);

      if (strncmp(header, "WDB2", 4) == 0)
        result = new WDB2File(fileToOpen->fullname());
      else if (strncmp(header, "WDB5", 4) == 0)
        result = new WDB5File(fileToOpen->fullname());
      else if (strncmp(header, "WDB6", 4) == 0)
        result = new WDB6File(fileToOpen->fullname());

      // reset read pointer to make sure further reading will start from the begining
      fileToOpen->seek(0);
    }
  }

  return result;
}

bool wow::TableStructure::create()
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

bool wow::TableStructure::fill()
{
  LOG_INFO << "Filling table" << name << "...";
  GameFile * fileToOpen = 0;
  // loop over possible extension to check if file exists
  for(unsigned int i=0 ; i < POSSIBLE_DB_EXT.size() ; i++)
  {
    fileToOpen = GAMEDIRECTORY.getFile("DBFilesClient\\"+gamefile+POSSIBLE_DB_EXT[i]);
    if(fileToOpen)
      break;
  }

  if(!fileToOpen)
    return false;

  DBFile * dbc = wow::WoWDatabase::createDBFile(fileToOpen);
  if(!dbc || !dbc->open())
    return false;

  QString query = "INSERT INTO ";
  query += name;
  query += "(";
  int nbFields = fields.size();
  int curfield = 0;
  for(auto it = fields.begin(), itEnd = fields.end();
      it != itEnd ;
      ++it,curfield++)
  {
	  if (it->arraySize == 1) // simple field
	  {
	    query += it->name;
	  }
	  else
	  {
	    for (unsigned int i = 1; i <= it->arraySize; i++)
	    {
		  query += it->name;
		  query += QString::number(i);
		  if (i != it->arraySize)
		    query += ",";
	    }
	  }
      if(curfield != nbFields-1)
        query += ",";
  }

  query += ") VALUES";
  
  QString queryBase = query;
  int record = 0;
  int nbRecord = dbc->getRecordCount();

  for (DBFile::Iterator it = dbc->begin(), itEnd = dbc->end(); it != itEnd; ++it, record++)
  {
    std::vector<std::string> fields = it.get(*this);

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
    // inserting all records at once makes the application crash, so
    // insert in chunks of 200 lines. If it's the last record anyway
    // then don't, as the final query after the for() loop will do it:
    if(record%200 == 0 && record != nbRecord-1)
    {
      query += ";";
      sqlResult r = GAMEDATABASE.sqlQuery(query);
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
  sqlResult r = GAMEDATABASE.sqlQuery(query);

  if (r.valid)
    LOG_INFO << "table" << name << "successfuly filled";

  delete dbc;

  return r.valid;
}

