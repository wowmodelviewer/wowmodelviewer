#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "DBDFile.h"

class GameFile;

namespace core
{
	/// @brief Describes a single field (column) in a database table.
	class FieldStructure
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

	/// @brief Describes the schema of a database table (name, file path, fields).
	class TableStructure
	{
	public:
		TableStructure() {}

		virtual ~TableStructure();

		std::string name;
		std::string file;
		std::vector<FieldStructure*> fields;
	};

	class GameDatabase
	{
	public:
		GameDatabase();

		bool initFromDBD(const std::string& dbdDir, const std::string& buildVersion);
		bool initFromDBD(const std::string& dbdDir, const std::string& buildVersion,
						 const std::vector<std::string>& tableNames);

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

		virtual core::TableStructure* createTableStructure() = 0;
		virtual core::FieldStructure* createFieldStructure() = 0;

		virtual void readSpecificTableAttributesFromDBD(const core::DBDVersionDef&, core::TableStructure*) = 0;
		virtual void readSpecificFieldAttributesFromDBD(const core::DBDVersionField&, const core::DBDColumnDef&,
														core::FieldStructure*) = 0;
		virtual void setFieldPos(core::FieldStructure*, int pos) = 0;

		// Get the layout hash from the actual DB2 file for a given table name.
		virtual std::string getLayoutHashForTable(const std::string& tableName) { return ""; }

	protected:
		// Build a TableStructure from a table name by parsing its DBD definition.
		// Returns nullptr on failure.
		core::TableStructure* buildTableStructure(const std::string& tableName);

	private:
		// Stored from initFromDBD for lazy per-table use.
		std::string m_dbdDir;
		core::DBDBuild m_build;

		bool m_fastMode;
		std::string m_cachePath;
		std::string m_dbdBaseUrl;
		std::string m_manifestUrl;
		std::unordered_map<std::string, int> m_tableFileDataIds;
	};
}
