#include "GameDatabase.h"
#include "dbfile.h"
#include "CSVFile.h"
#include "logger/Logger.h"
#include "Game.h"

#include <string>
#include <vector>

#include "sqlite3.h"

core::GameDatabase::~GameDatabase()
{
	if (m_db)
		sqlite3_close(m_db);
}

core::GameDatabase::GameDatabase() : m_db(nullptr), m_fastMode(false)
{
}

bool core::GameDatabase::initFromXML(const std::string& file)
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

	return createDatabaseFromXML(core::Game::instance().configFolder() + file);
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

bool core::GameDatabase::createDatabaseFromXML(const std::string& file)
{
	if (!readStructureFromXML(file))
	{
		LOG_ERROR << "Reading database structure from XML file failed ! Impossible to create database.";
		return false;
	}

	bool result = true; // ok until we found an issue

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
			if (!m_fastMode) // if table already exists in fast mode, continue
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

void core::GameDatabase::logQueryTime(void* aDb, const char* aQueryStr, sqlite3_uint64 aTimeInNs)
{
	if (aTimeInNs / 1000000 > 50)
	{
		LOG_WARNING << "LONG QUERY !";
		LOG_WARNING << aQueryStr;
		LOG_WARNING << "Query time (ms)" << aTimeInNs / 1000000;
	}
}

bool core::GameDatabase::readStructureFromXML(const std::string& file)
{
	pugi::xml_document doc;
	const pugi::xml_parse_result parseResult = doc.load_file(file.c_str());

	if (!parseResult)
	{
		LOG_ERROR << "XML parse error:" << parseResult.description() << "at offset" << parseResult.offset;
		return false;
	}

	const pugi::xml_node docElem = doc.document_element();

	for (pugi::xml_node e = docElem.first_child(); e; e = e.next_sibling())
	{
		core::TableStructure* tblStruct = createTableStructure();
		pugi::xml_node child = e.first_child();

		// table values
		tblStruct->name = e.attribute("name").as_string();

		const pugi::xml_attribute dbfile = e.attribute("dbfile");
		if (dbfile)
			tblStruct->file = dbfile.as_string();
		else
			tblStruct->file = tblStruct->name;

		readSpecificTableAttributes(child, tblStruct);

		int fieldId = 0;
		for (; child; child = child.next_sibling(), fieldId++)
		{
			core::FieldStructure* fieldStruct = createFieldStructure();
			fieldStruct->id = fieldId;

			// search if name and type are here
			const pugi::xml_attribute name = child.attribute("name");
			const pugi::xml_attribute type = child.attribute("type");
			const pugi::xml_attribute key = child.attribute("primary");
			const pugi::xml_attribute arraySize = child.attribute("arraySize");
			const pugi::xml_attribute index = child.attribute("createIndex");

			if (name && type)
			{
				fieldStruct->name = name.as_string();
				fieldStruct->type = type.as_string();

				if (key)
					fieldStruct->isKey = true;

				if (index)
					fieldStruct->needIndex = true;

				if (arraySize)
					fieldStruct->arraySize = arraySize.as_uint();

				readSpecificFieldAttributes(child, fieldStruct);

				tblStruct->fields.push_back(fieldStruct);
			}
		}

		addTable(tblStruct);
	}
	return true;
}

bool core::TableStructure::create()
{
	LOG_INFO << "Creating table" << name;
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
