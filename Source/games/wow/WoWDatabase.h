#pragma once

#include "GameDatabase.h"

class DBFile;
class GameFile;

#define _WOWDATABASE_API_

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

		void readSpecificTableAttributes(const pugi::xml_node&, core::TableStructure*);
		void readSpecificFieldAttributes(const pugi::xml_node&, core::FieldStructure*);
	};
}
