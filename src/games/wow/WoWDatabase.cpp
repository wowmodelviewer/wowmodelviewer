/*
 * WoWDatabase.cpp
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#include "WoWDatabase.h"

#include <QDomElement>
#include <QDomNamedNodeMap>

#include "Game.h"
#include "logger/Logger.h"
#include "wdb2file.h"
#include "wdb5file.h"
#include "wdb6file.h"

const std::vector<QString> POSSIBLE_DB_EXT = {".db2", ".dbc"};

wow::WoWDatabase::WoWDatabase()
  : GameDatabase()
{

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

core::TableStructure *  wow::WoWDatabase::createTableStructure()
{
  return new wow::TableStructure;
}

core::FieldStructure *  wow::WoWDatabase::createFieldStructure()
{
  return new wow::FieldStructure;
}

void wow::WoWDatabase::readSpecificTableAttributes(QDomElement & e, core::TableStructure * tblStruct)
{
  wow::TableStructure * tbl = dynamic_cast<wow::TableStructure *>(tblStruct);

  if (!tbl)
    return;

  QDomNamedNodeMap attributes = e.attributes();
  QDomNode hash = attributes.namedItem("layoutHash");

  if (!hash.isNull())
    tbl->hash = hash.nodeValue().toUInt();
}

void wow::WoWDatabase::readSpecificFieldAttributes(QDomElement & e, core::FieldStructure * fieldStruct)
{
  wow::FieldStructure * field = dynamic_cast<wow::FieldStructure *>(fieldStruct);

  if (!field)
    return;

  QDomNamedNodeMap attributes = e.attributes();

  QDomNode pos = attributes.namedItem("pos");
  QDomNode commonData = attributes.namedItem("commonData");

  if (!pos.isNull())
    field->pos = pos.nodeValue().toInt();

  if (!commonData.isNull())
    field->isCommonData = true;

}

bool wow::TableStructure::fill()
{
  LOG_INFO << "Filling table" << name << "...";
  GameFile * fileToOpen = 0;
  // loop over possible extension to check if file exists
  for(unsigned int i=0 ; i < POSSIBLE_DB_EXT.size() ; i++)
  {
    fileToOpen = GAMEDIRECTORY.getFile("DBFilesClient\\"+file+POSSIBLE_DB_EXT[i]);
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
	  if ((*it)->arraySize == 1) // simple field
	  {
	    query += (*it)->name;
	  }
	  else
	  {
	    for (unsigned int i = 1; i <= (*it)->arraySize; i++)
	    {
		  query += (*it)->name;
		  query += QString::number(i);
		  if (i != (*it)->arraySize)
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

