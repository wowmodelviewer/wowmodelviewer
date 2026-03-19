#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "sqlite3.h"
#include "DBDFile.h"

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

		bool initFromDBD(const std::string& dbdDir, const std::string& buildVersion,
						 const std::vector<std::string>& tableNames);

		sqlResult sqlQuery(const std::string& query);

		void setFastMode() { m_fastMode = true; }
		void setCachePath(const std::string& path) { m_cachePath = path; }
		void setDbdBaseUrl(const std::string& url) { m_dbdBaseUrl = url; }
		void setManifestUrl(const std::string& url) { m_manifestUrl = url; }

		// Download and parse the DBD manifest.json to populate tableName -> fileDataID map.
		// Call this before initFromDBD() so file data IDs are available for CASC lookups.
		bool downloadAndParseManifest();

		// Returns the db2 file data ID for a given table name, or 0 if not found.
		int getFileDataIdForTable(const std::string& tableName) const;

		virtual ~GameDatabase();

		void addTable(TableStructure*);

		virtual core::TableStructure* createTableStructure() = 0;
		virtual core::FieldStructure* createFieldStructure() = 0;

		virtual void readSpecificTableAttributesFromDBD(const core::DBDVersionDef&, core::TableStructure*) = 0;
		virtual void readSpecificFieldAttributesFromDBD(const core::DBDVersionField&, const core::DBDColumnDef&,
														core::FieldStructure*) = 0;
		virtual void setFieldPos(core::FieldStructure*, int pos) = 0;

		// Get the layout hash from the actual DB2 file for a given table name.
		// Returns the hash as an uppercase hex string, or empty string if unavailable.
		// Override in game-specific subclasses to read from game files.
		virtual std::string getLayoutHashForTable(const std::string& tableName) { return ""; }

	private:
		static int treatQuery(void* NotUsed, int nbcols, char** values, char** cols);
		static void logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs);

		bool createDatabaseFromDBD(const std::string& dbdDir, const core::DBDBuild& build,
								   const std::vector<std::string>& tableNames);
		bool readStructureFromDBD(const std::string& dbdDir, const core::DBDBuild& build,
								  const std::vector<std::string>& tableNames);

		sqlite3* m_db;

		std::vector<TableStructure*> m_dbStruct;

		bool m_fastMode;
		std::string m_cachePath;
		std::string m_dbdBaseUrl;
		std::string m_manifestUrl;
		std::unordered_map<std::string, int> m_tableFileDataIds;
	};
}
