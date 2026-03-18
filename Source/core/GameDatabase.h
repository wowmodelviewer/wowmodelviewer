#pragma once

#include <string>
#include <vector>
#include "sqlite3.h"
#include <pugixml.hpp>

class DBFile;
class GameFile;

#define _GAMEDATABASE_API_

class _GAMEDATABASE_API_ sqlResult
{
public:
	sqlResult() : valid(false), nbcols(0)
	{
	}

	~sqlResult()
	{
		/* TODO :free char** */
	}

	bool empty() { return values.size() == 0; }
	bool valid;
	int nbcols;
	std::vector<std::vector<std::string>> values;
};

namespace core
{
	// table structures as defined in xml file
	class _GAMEDATABASE_API_ FieldStructure
	{
	public:
		FieldStructure() :
			isKey(false),
			needIndex(false),
			arraySize(1),
			id(0)
		{
		}

		virtual ~FieldStructure()
		{
		}

		std::string name;
		std::string type;
		bool isKey;
		bool needIndex;
		unsigned int arraySize;
		int id;
	};

	class _GAMEDATABASE_API_ TableStructure
	{
	public:
		TableStructure() {}

		virtual ~TableStructure();

		std::string name;
		std::string file;
		std::vector<FieldStructure*> fields;

		bool create();
		bool fill();

		virtual DBFile* createDBFile();
	};

	class _GAMEDATABASE_API_ GameDatabase
	{
	public:
		GameDatabase();
		//GameDatabase(GameDatabase&);

		bool initFromXML(const std::string& file);

		sqlResult sqlQuery(const std::string& query);

		void setFastMode() { m_fastMode = true; }
		void setCachePath(const std::string& path) { m_cachePath = path; }

		virtual ~GameDatabase();

		void addTable(TableStructure*);

		virtual core::TableStructure* createTableStructure() = 0;
		virtual core::FieldStructure* createFieldStructure() = 0;

		virtual void readSpecificTableAttributes(const pugi::xml_node&, core::TableStructure*) = 0;
		virtual void readSpecificFieldAttributes(const pugi::xml_node&, core::FieldStructure*) = 0;

	private:
		static int treatQuery(void* NotUsed, int nbcols, char** values, char** cols);
		static void logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs);

		bool createDatabaseFromXML(const std::string& file);
		bool readStructureFromXML(const std::string& file);

		sqlite3* m_db;

		std::vector<TableStructure*> m_dbStruct;

		bool m_fastMode;
		std::string m_cachePath;
	};
}
