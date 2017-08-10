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

#include "GameDatabase.h"

class DBFile;
class GameFile;

class QDomElement;
#include <QString>


#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WOWDATABASE_API_ __declspec(dllexport)
#    else
#        define _WOWDATABASE_API_ __declspec(dllimport)
#    endif
#else
#    define _WOWDATABASE_API_
#endif

namespace wow
{
  class TableStructure : public core::TableStructure
  {
  public:
    virtual bool fill();
  };

  class _WOWDATABASE_API_ WoWDatabase : public core::GameDatabase
  {
    public:
      WoWDatabase();
      WoWDatabase(WoWDatabase &);

      ~WoWDatabase() {}

      static DBFile * createDBFile(GameFile *);

    protected:
      virtual bool readStructureFromXML(const QString & file);

  };

}

#endif /* _WOWDATABASE_H_ */
