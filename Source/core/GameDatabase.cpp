#include "GameDatabase.h"
#include "dbfile.h"
#include "CSVFile.h"
#include "logger/Logger.h"
#include "Game.h"
#include "DBDFile.h"
#include "string_utils.h"
#include "HttpClient.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sqlite3.h"
#include <nlohmann/json.hpp>

core::GameDatabase::~GameDatabase()
{
	if (m_db)
		sqlite3_close(m_db);
}

core::GameDatabase::GameDatabase() : m_db(nullptr), m_fastMode(false)
{
}

bool core::GameDatabase::downloadAndParseManifest()
{
	if (m_manifestUrl.empty())
	{
		LOG_ERROR << "Manifest URL not set.";
		return false;
	}

	LOG_INFO << "Downloading DBD manifest from " << m_manifestUrl;
	const auto resp = HttpClient::Get(m_manifestUrl);
	if (!resp.success || resp.body.empty())
	{
		LOG_ERROR << "Failed to download manifest:" << resp.error;
		return false;
	}

	try
	{
		const auto manifest = nlohmann::json::parse(resp.body);
		for (const auto& entry : manifest)
		{
			if (entry.contains("tableName") && entry.contains("db2FileDataID"))
			{
				const std::string tableName = entry["tableName"].get<std::string>();
				const int fileDataId = entry["db2FileDataID"].get<int>();
				if (!tableName.empty() && fileDataId > 0)
					m_tableFileDataIds[tableName] = fileDataId;
			}
		}

		LOG_INFO << "Loaded DBD manifest with " << m_tableFileDataIds.size() << " entries ";
		return true;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR << "Failed to parse manifest JSON:" << e.what();
		return false;
	}
}

int core::GameDatabase::getFileDataIdForTable(const std::string& tableName) const
{
	const auto it = m_tableFileDataIds.find(tableName);
	if (it != m_tableFileDataIds.end())
		return it->second;
	return 0;
}

sqlResult core::GameDatabase::sqlQuery(const std::string& query)
{
	sqlResult result;

	char* zErrMsg = nullptr;
	const int rc = sqlite3_exec(m_db, query.c_str(), core::GameDatabase::treatQuery, (void*)&result, &zErrMsg);
	if (rc != SQLITE_OK)
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

void core::GameDatabase::addTable(TableStructure* tbl)
{
	m_dbStruct.push_back(tbl);
}

int core::GameDatabase::treatQuery(void* resultPtr, int nbcols, char** vals, char** cols)
{
	sqlResult* r = static_cast<sqlResult*>(resultPtr);
	if (!r)
		return 1;

	std::vector<std::string> values;
	// update columns
	for (int i = 0; i < nbcols; i++)
	{
		values.emplace_back(vals[i] ? vals[i] : "");
	}

	r->values.push_back(values);
	r->nbcols = nbcols;

	return 0;
}

void core::GameDatabase::logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs)
{
	if (aTimeInNs / 1000000 > 50)
	{
		LOG_WARNING << "LONG QUERY !";
		LOG_WARNING << aQueryStr;
		LOG_WARNING << "Query time (ms)" << aTimeInNs / 1000000;
	}
}

bool core::TableStructure::create()
{
	LOG_INFO << "Creating table " << name;
	std::string create = "CREATE TABLE " + name + " (";

	std::vector<std::string> indexesToCreate;

	for (const auto& field : fields)
	{
		if (field->arraySize == 1) // simple field
		{
			create += field->name;
			create += " ";
			create += field->type;

			if (field->isKey)
				create += " PRIMARY KEY NOT NULL";

			create += ",";
		}
		else // complex field
		{
			for (unsigned int i = 1; i <= field->arraySize; i++)
			{
				create += field->name;
				create += std::to_string(i);
				create += " ";
				create += field->type;
				create += ",";
			}
		}

		if (field->needIndex)
			indexesToCreate.push_back(field->name);
	}

	// remove spurious "," at the end of string, if any
	if (!create.empty() && create.back() == ',')
		create.pop_back();
	create += ");";

	//LOG_INFO << create;

	const sqlResult r = core::Game::instance().database().sqlQuery(create);

	if (r.valid)
	{
		LOG_INFO << "Table" << name << "successfully created";

		// create indexes
		for (auto& it : indexesToCreate)
		{
			std::string query = "CREATE INDEX " + name + "_" + it + " ON " + name + "(" + it + ")";
			core::Game::instance().database().sqlQuery(query);
		}
	}

	return r.valid;
}

bool core::TableStructure::fill()
{
	LOG_INFO << "Filling table" << name << "...";

	DBFile* dbc = createDBFile();
	if (!dbc || !dbc->open())
		return false;

	std::string query = "INSERT INTO ";
	query += name;
	query += "(";
	const auto nbFields = fields.size();
	int curfield = 0;
	for (auto it = fields.begin(), itEnd = fields.end();
		 it != itEnd;
		 ++it, curfield++)
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
				query += std::to_string(i);
				if (i != (*it)->arraySize)
					query += ",";
			}
		}
		if (curfield != nbFields - 1)
			query += ",";
	}

	query += ") VALUES";

	const std::string queryBase = query;
	int record = 0;
	const auto nbRecord = dbc->getRecordCount();

	for (DBFile::Iterator it = dbc->begin(), itEnd = dbc->end(); it != itEnd; ++it, record++)
	{
		std::vector<std::string> Fields = it.get(this);

		for (size_t field = 0, nbfield = Fields.size(); field < nbfield; field++)
		{
			if (field == 0)
				query += " (";
			query += "\"";
			query += Fields[field];
			query += "\"";
			if (field != nbfield - 1)
				query += ",";
			else
				query += ")";
		}
		// inserting all records at once makes the application crash, so
		// insert in chunks of 200 lines. If it's the last record anyway
		// then don't, as the final query after the for() loop will do it:
		if (record % 200 == 0 && record != nbRecord - 1)
		{
			query += ";";
			const sqlResult r = GAMEDATABASE.sqlQuery(query);
			if (!r.valid)
				return false;
			query = queryBase;
		}
		else
		{
			if (record != nbRecord - 1)
				query += ",";
		}
	}

	query += ";";
	const sqlResult r = GAMEDATABASE.sqlQuery(query);

	if (r.valid)
		LOG_INFO << "table" << name << "successfuly filled";

	delete dbc;

	return r.valid;
}

DBFile* core::TableStructure::createDBFile()
{
	DBFile* result = nullptr;
	if (file.find(".csv") != std::string::npos)
		result = new CSVFile(file);

	return result;
}

core::TableStructure::~TableStructure()
{
	for (const auto it : fields)
		delete it;
}

// --- DBD support ---

bool core::GameDatabase::initFromDBD(const std::string& dbdDir, const std::string& buildVersion,
									 const std::vector<std::string>& tableNames)
{
	int rc;

	if (m_fastMode)
	{
		const char* path = m_cachePath.empty() ? "./wowdb.sqlite" : m_cachePath.c_str();
		rc = sqlite3_open(path, &m_db);
	}
	else
		rc = sqlite3_open(":memory:", &m_db);

	if (rc)
	{
		LOG_INFO << "Can't open database:" << sqlite3_errmsg(m_db);
		return false;
	}
	else
	{
		LOG_INFO << "Opened database successfully";
	}

	sqlite3_profile(m_db, GameDatabase::logQueryTime, m_db);

	const core::DBDBuild build = core::DBDBuild::fromString(buildVersion);

	return createDatabaseFromDBD(dbdDir, build, tableNames);
}

bool core::GameDatabase::createDatabaseFromDBD(const std::string& dbdDir, const core::DBDBuild& build,
											   const std::vector<std::string>& tableNames)
{
	if (!readStructureFromDBD(dbdDir, build, tableNames))
	{
		LOG_ERROR << "Reading database structure from DBD files failed! Impossible to create database.";
		return false;
	}

	bool result = true;

	for (const auto& it : m_dbStruct)
	{
		if (it->create())
		{
			if (!it->fill() && !m_fastMode)
			{
				LOG_ERROR << "Error during table filling" << it->name;
				result = false;
			}
		}
		else
		{
			if (!m_fastMode)
			{
				LOG_ERROR << "Error during table creation" << it->name;
				result = false;
			}
		}
	}

	for (const auto it : m_dbStruct)
		delete it;

	return result;
}

static std::string dbdTypeToSqlType(const std::string& baseType, const std::string& sizeStr)
{
	if (baseType == "string" || baseType == "locstring")
		return "text";

	if (baseType == "float")
		return "float";

	// int type - determine signed/unsigned and bit size from sizeStr
	if (baseType == "int")
	{
		if (sizeStr.empty())
			return "int32"; // default

		bool isUnsigned = false;
		std::string numStr = sizeStr;

		if (!numStr.empty() && (numStr[0] == 'u' || numStr[0] == 'U'))
		{
			isUnsigned = true;
			numStr = numStr.substr(1);
		}

		std::string prefix = isUnsigned ? "uint" : "int";
		return prefix + numStr;
	}

	return "int32";
}

bool core::GameDatabase::readStructureFromDBD(const std::string& dbdDir, const core::DBDBuild& build,
											  const std::vector<std::string>& tableNames)
{
	namespace fs = std::filesystem;

	for (const auto& entry : tableNames)
	{
		// Check for CSV table definition: CSV:TableName:filename.csv:type1:name1:primary:type2:name2:...
		if (entry.starts_with("CSV:"))
		{
			auto parts = core::split(entry, ':');
			if (parts.size() < 5)
			{
				LOG_ERROR << "Invalid CSV table definition:" << entry;
				continue;
			}

			const std::string& tableName = parts[1];
			const std::string& csvFile = parts[2];

			core::TableStructure* tblStruct = createTableStructure();
			tblStruct->name = tableName;
			tblStruct->file = csvFile;

			int fieldId = 0;
			size_t i = 3;
			while (i + 1 < parts.size())
			{
				const std::string& fieldType = parts[i];
				const std::string& fieldName = parts[i + 1];

				// Check if the next token is "primary"
				bool isPrimary = false;
				if (i + 2 < parts.size() && parts[i + 2] == "primary")
				{
					isPrimary = true;
					i += 3; // skip type, name, primary
				}
				else
				{
					i += 2; // skip type, name
				}

				core::FieldStructure* fieldStruct = createFieldStructure();
				fieldStruct->id = fieldId++;
				fieldStruct->name = fieldName;
				fieldStruct->type = fieldType;
				fieldStruct->isKey = isPrimary;
				fieldStruct->arraySize = 1;
				fieldStruct->needIndex = false;

				tblStruct->fields.push_back(fieldStruct);
			}

			addTable(tblStruct);
			continue;
		}

		// Regular DBD table
		const std::string& tableName = entry;
		std::string dbdPath = dbdDir + "/" + tableName + ".dbd";

		// If local file does not exist and we have a base URL, download on demand
		if (!fs::exists(dbdPath) && !m_dbdBaseUrl.empty())
		{
			std::string url = m_dbdBaseUrl;
			// Replace %s placeholder with table name, or just append
			auto pos = url.find("%s");
			if (pos != std::string::npos)
				url.replace(pos, 2, tableName);
			else
				url += tableName + ".dbd";

			LOG_INFO << "Downloading DBD for" << tableName << "from" << url;
			const auto resp = HttpClient::Get(url);
			if (resp.success && !resp.body.empty())
			{
				// Cache to local directory
				fs::create_directories(dbdDir);
				std::ofstream out(dbdPath, std::ios::binary);
				if (out.is_open())
				{
					out.write(resp.body.data(), resp.body.size());
					out.close();
				}
			}
			else
			{
				LOG_ERROR << "Failed to download DBD for" << tableName << ":" << resp.error;
			}
		}

		if (!fs::exists(dbdPath))
		{
			LOG_ERROR << "DBD file not found:" << dbdPath;
			continue;
		}

		core::DBDFile dbd;
		if (!dbd.parse(dbdPath))
		{
			LOG_ERROR << "Failed to parse DBD file:" << dbdPath;
			continue;
		}

		const std::string layoutHash = getLayoutHashForTable(tableName);
		const core::DBDVersionDef* verDef = dbd.findVersion(build, layoutHash);
		if (!verDef)
		{
			LOG_ERROR << "No matching version definition found in" << dbdPath
					  << "for build" << build.major << "." << build.minor
					  << "." << build.patch << "." << build.build;
			continue;
		}

		core::TableStructure* tblStruct = createTableStructure();
		tblStruct->name = tableName;
		tblStruct->file = tableName;

		readSpecificTableAttributesFromDBD(*verDef, tblStruct);

		int fieldId = 0;
		int fieldPos = 0; // position index in DB2 for non-id/non-noninline fields

		for (const auto& vField : verDef->fields)
		{
			const core::DBDColumnDef* colDef = dbd.findColumn(vField.name);
			if (!colDef)
			{
				LOG_ERROR << "Column definition not found for field" << vField.name
						  << "in table" << tableName;
				// Still need to advance pos for inline fields even on error
					if (!vField.isNonInline)
						fieldPos++;
				continue;
			}

			core::FieldStructure* fieldStruct = createFieldStructure();
			fieldStruct->id = fieldId++;

			fieldStruct->name = vField.name;
			fieldStruct->type = dbdTypeToSqlType(colDef->type, vField.sizeStr);
			fieldStruct->arraySize = vField.arraySize;
			fieldStruct->isKey = vField.isID;
			fieldStruct->needIndex = false;

			// Pass the current field position to the game-specific handler
			readSpecificFieldAttributesFromDBD(vField, *colDef, fieldStruct);

			// Set the DB2 field position for inline fields
			if (!vField.isNonInline)
			{
				setFieldPos(fieldStruct, fieldPos);
				fieldPos++;
			}

			tblStruct->fields.push_back(fieldStruct);
		}

		addTable(tblStruct);
	}

	return !m_dbStruct.empty();
}
