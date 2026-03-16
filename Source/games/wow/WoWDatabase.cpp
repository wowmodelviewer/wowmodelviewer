#include "WoWDatabase.h"
#include "Game.h"
#include "logger/Logger.h"
#include "wdb2file.h"
#include "wdb5file.h"
#include "wdb6file.h"
#include "wdc1file.h"
#include "wdc2file.h"
#include "wdc3file.h"
#include "wdc5file.h"

const std::vector<std::string> POSSIBLE_DB_EXT = {".db2", ".dbc"};

wow::WoWDatabase::WoWDatabase()
{
}

core::TableStructure* wow::WoWDatabase::createTableStructure()
{
	return new wow::TableStructure;
}

core::FieldStructure* wow::WoWDatabase::createFieldStructure()
{
	return new wow::FieldStructure;
}

void wow::WoWDatabase::readSpecificTableAttributes(const pugi::xml_node& e, core::TableStructure* tblStruct)
{
	wow::TableStructure* tbl = dynamic_cast<wow::TableStructure*>(tblStruct);

	if (!tbl)
		return;

	const pugi::xml_attribute hash = e.attribute("layoutHash");

	if (hash)
		tbl->hash = hash.as_uint();
}

void wow::WoWDatabase::readSpecificFieldAttributes(const pugi::xml_node& e, core::FieldStructure* fieldStruct)
{
	wow::FieldStructure* field = dynamic_cast<wow::FieldStructure*>(fieldStruct);

	if (!field)
		return;

	const pugi::xml_attribute pos = e.attribute("pos");
	const pugi::xml_attribute commonData = e.attribute("commonData");
	const pugi::xml_attribute relationshipData = e.attribute("relationshipData");

	if (pos)
		field->pos = pos.as_int();

	if (commonData)
		field->isCommonData = true;

	if (relationshipData)
		field->isRelationshipData = true;
}

DBFile* wow::TableStructure::createDBFile()
{
	DBFile* result = core::TableStructure::createDBFile();

	if (result != nullptr)
		return result;

	GameFile* fileToOpen = nullptr;
	// loop over possible extension to check if file exists
	for (const auto& i : POSSIBLE_DB_EXT)
	{
		fileToOpen = GAMEDIRECTORY.getFile(QString::fromStdString("DBFilesClient\\" + file + i));
		if (fileToOpen)
			break;
	}

	if (!fileToOpen)
		return nullptr;

	if (fileToOpen->open(false))
	{
		char header[5];

		fileToOpen->read(header, 4);

		if (strncmp(header, "WDB2", 4) == 0)
			result = new WDB2File(fileToOpen->fullname());
		else if (strncmp(header, "WDB5", 4) == 0)
			result = new WDB5File(fileToOpen->fullname());
		else if (strncmp(header, "WDB6", 4) == 0)
			result = new WDB6File(fileToOpen->fullname());
		else if (strncmp(header, "WDC1", 4) == 0)
			result = new WDC1File(fileToOpen->fullname());
		else if (strncmp(header, "WDC2", 4) == 0)
			result = new WDC2File(fileToOpen->fullname());
		else if (strncmp(header, "WDC3", 4) == 0)
			result = new WDC3File(fileToOpen->fullname());
		else if (strncmp(header, "WDC4", 4) == 0)
			result = new WDC3File(fileToOpen->fullname());
		else if (strncmp(header, "WDC5", 4) == 0)
			result = new WDC5File(fileToOpen->fullname());
		else
			LOG_ERROR << "Unsupported database file" << header[0] << header[1] << header[2] << header[3];

		fileToOpen->close();
	}

	return result;
}
