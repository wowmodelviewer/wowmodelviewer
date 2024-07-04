#pragma once

#include "GameDatabase.h"

class DBFile;
class GameFile;

class QDomElement;

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
		TableStructure() : hash(0)
		{
		}

		unsigned int hash;

		DBFile* createDBFile();
	};

	class FieldStructure : public core::FieldStructure
	{
	public:
		FieldStructure() : pos(-1), isCommonData(false), isRelationshipData(false)
		{
		}

		int pos;
		bool isCommonData;
		bool isRelationshipData;
	};

	class _WOWDATABASE_API_ WoWDatabase : public core::GameDatabase
	{
	public:
		WoWDatabase();
		//WoWDatabase(WoWDatabase&);

		~WoWDatabase()
		{
		}

		core::TableStructure* createTableStructure();
		core::FieldStructure* createFieldStructure();

		void readSpecificTableAttributes(QDomElement&, core::TableStructure*);
		void readSpecificFieldAttributes(QDomElement&, core::FieldStructure*);
	};
}
