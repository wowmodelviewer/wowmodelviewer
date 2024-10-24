#include "GameDatabase.h"
#include "dbfile.h"
#include "CSVFile.h"
#include <QDomElement>
#include <QFile>
#include "logger/Logger.h"
#include "Game.h"

core::GameDatabase::~GameDatabase()
{
	if (m_db)
		sqlite3_close(m_db);
}

core::GameDatabase::GameDatabase() : m_db(nullptr), m_fastMode(false)
{
}

bool core::GameDatabase::initFromXML(const QString& file)
{
	int rc;

	if (m_fastMode)
		rc = sqlite3_open("./wowdb.sqlite", &m_db);
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

sqlResult core::GameDatabase::sqlQuery(const QString& query)
{
	sqlResult result;

	char* zErrMsg = nullptr;
	const int rc = sqlite3_exec(m_db, query.toStdString().c_str(), core::GameDatabase::treatQuery, (void*)&result, &zErrMsg);
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

	std::vector<QString> values;
	// update columns
	for (int i = 0; i < nbcols; i++)
	{
		values.emplace_back(vals[i]);
	}

	r->values.push_back(values);
	r->nbcols = nbcols;

	return 0;
}

bool core::GameDatabase::createDatabaseFromXML(const QString& file)
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

bool core::GameDatabase::readStructureFromXML(const QString& file)
{
	QDomDocument doc;

	QFile f(file);
	f.open(QIODevice::ReadOnly);
	doc.setContent(&f);
	f.close();

	const QDomElement docElem = doc.documentElement();

	QDomElement e = docElem.firstChildElement();

	while (!e.isNull())
	{
		core::TableStructure* tblStruct = createTableStructure();
		QDomElement child = e.firstChildElement();

		QDomNamedNodeMap attributes = e.attributes();
		QDomNode dbfile = attributes.namedItem("dbfile");

		// table values
		tblStruct->name = attributes.namedItem("name").nodeValue();

		if (!dbfile.isNull())
			tblStruct->file = dbfile.nodeValue();
		else
			tblStruct->file = tblStruct->name;

		readSpecificTableAttributes(child, tblStruct);

		int fieldId = 0;
		while (!child.isNull())
		{
			core::FieldStructure* fieldStruct = createFieldStructure();
			fieldStruct->id = fieldId;
			QDomNamedNodeMap Attributes = child.attributes();

			// search if name and type are here
			QDomNode name = Attributes.namedItem("name");
			QDomNode type = Attributes.namedItem("type");
			QDomNode key = Attributes.namedItem("primary");
			QDomNode arraySize = Attributes.namedItem("arraySize");
			QDomNode index = Attributes.namedItem("createIndex");

			if (!name.isNull() && !type.isNull())
			{
				fieldStruct->name = name.nodeValue();
				fieldStruct->type = type.nodeValue();

				if (!key.isNull())
					fieldStruct->isKey = true;

				if (!index.isNull())
					fieldStruct->needIndex = true;

				if (!arraySize.isNull())
					fieldStruct->arraySize = arraySize.nodeValue().toUInt();

				readSpecificFieldAttributes(child, fieldStruct);

				tblStruct->fields.push_back(fieldStruct);
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

bool core::TableStructure::create()
{
	LOG_INFO << "Creating table" << name;
	QString create = "CREATE TABLE " + name + " (";

	std::list<QString> indexesToCreate;

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
				create += QString::number(i);
				create += " ";
				create += field->type;
				create += ",";
			}
		}

		if (field->needIndex)
			indexesToCreate.push_back(field->name);
	}

	// remove spurious "," at the end of string, if any
	if (create.lastIndexOf(",") == create.length() - 1)
		create.remove(create.length() - 1, 1);
	create += ");";

	//LOG_INFO << create;

	const sqlResult r = core::Game::instance().database().sqlQuery(create);

	if (r.valid)
	{
		LOG_INFO << "Table" << name << "successfully created";

		// create indexes
		for (auto& it : indexesToCreate)
		{
			QString query = QString("CREATE INDEX %1_%2 ON %1(%2)").arg(name).arg(it);
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

	QString query = "INSERT INTO ";
	query += name;
	query += "(";
	const int nbFields = fields.size();
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
				query += QString::number(i);
				if (i != (*it)->arraySize)
					query += ",";
			}
		}
		if (curfield != nbFields - 1)
			query += ",";
	}

	query += ") VALUES";

	const QString queryBase = query;
	int record = 0;
	const int nbRecord = dbc->getRecordCount();

	for (DBFile::Iterator it = dbc->begin(), itEnd = dbc->end(); it != itEnd; ++it, record++)
	{
		std::vector<std::string> Fields = it.get(this);

		for (int field = 0, nbfield = Fields.size(); field < nbfield; field++)
		{
			if (field == 0)
				query += " (";
			query += "\"";
			query += QString::fromStdString(Fields[field]);
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
	if (file.contains(".csv"))
		result = new CSVFile(file);

	return result;
}

core::TableStructure::~TableStructure()
{
	for (const auto it : fields)
		delete it;
}
