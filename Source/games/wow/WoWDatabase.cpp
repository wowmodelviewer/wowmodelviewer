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
#include "WDCReader.h"
#include "DBDFile.h"

#include <sstream>
#include <cstdio>

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

void wow::WoWDatabase::readSpecificTableAttributesFromDBD(const core::DBDVersionDef& verDef,
														  core::TableStructure* tblStruct)
{
	wow::TableStructure* tbl = dynamic_cast<wow::TableStructure*>(tblStruct);
	if (!tbl)
		return;

	// Extract the layout hash from the version definition
	if (!verDef.layoutHashes.empty())
	{
		// Layout hash is a hex string - convert to uint32
		unsigned long val = std::stoul(verDef.layoutHashes[0], nullptr, 16);
		tbl->hash = static_cast<unsigned int>(val);
	}
}

void wow::WoWDatabase::readSpecificFieldAttributesFromDBD(const core::DBDVersionField& vField,
														  const core::DBDColumnDef& colDef,
														  core::FieldStructure* fieldStruct)
{
	wow::FieldStructure* field = dynamic_cast<wow::FieldStructure*>(fieldStruct);
	if (!field)
		return;

	field->isRelationshipData = vField.isRelation && vField.isNonInline;

	// Only noninline fields have no DB2 position
	if (vField.isNonInline)
		field->pos = -1;
}

void wow::WoWDatabase::setFieldPos(core::FieldStructure* fieldStruct, int pos)
{
	wow::FieldStructure* field = dynamic_cast<wow::FieldStructure*>(fieldStruct);
	if (field)
		field->pos = pos;
}

std::string wow::WoWDatabase::getLayoutHashForTable(const std::string& tableName)
{
	GameFile* fileToOpen = nullptr;
	for (const auto& ext : POSSIBLE_DB_EXT)
	{
		fileToOpen = GAMEDIRECTORY.getFile("DBFilesClient\\" + tableName + ext);
		if (fileToOpen)
			break;
	}

	// Fallback: try file data ID from manifest
	if (!fileToOpen)
	{
		const int fileDataId = GAMEDATABASE.getFileDataIdForTable(tableName);
		if (fileDataId > 0)
			fileToOpen = GAMEDIRECTORY.getFile(fileDataId);
	}

	if (!fileToOpen)
		return "";

	if (!fileToOpen->open(false))
		return "";

	char magic[4] = {};
	fileToOpen->read(magic, 4);

	// Determine offset to layout_hash based on format.
	// In the WDC3 header struct, layout_hash is at byte offset 24 from file start:
	//   magic(4) + record_count(4) + field_count(4) + record_size(4) + string_table_size(4) + table_hash(4)
	// WDC5 adds versionNum(4) + schemaString(128) = 132 extra bytes after magic.
	size_t layoutHashOffset = 0;

	if (strncmp(magic, "WDC5", 4) == 0)
		layoutHashOffset = 4 + 132 + 20; // 156
	else if (strncmp(magic, "WDC2", 4) == 0 || strncmp(magic, "WDC3", 4) == 0 || strncmp(magic, "WDC4", 4) == 0)
		layoutHashOffset = 24;
	else
	{
		fileToOpen->close();
		return "";
	}

	fileToOpen->seek(layoutHashOffset);
	unsigned int layoutHash = 0;
	fileToOpen->read(&layoutHash, sizeof(layoutHash));
	fileToOpen->close();

	// Convert to uppercase hex string to match DBD format (e.g. "0E84A21C")
	char hexBuf[9];
	std::snprintf(hexBuf, sizeof(hexBuf), "%08X", layoutHash);
	return std::string(hexBuf);
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
		fileToOpen = GAMEDIRECTORY.getFile("DBFilesClient\\" + file + i);
		if (fileToOpen)
			break;
	}

	// Fallback: try file data ID from manifest
	if (!fileToOpen)
	{
		const int fileDataId = GAMEDATABASE.getFileDataIdForTable(file);
		if (fileDataId > 0)
			fileToOpen = GAMEDIRECTORY.getFile(fileDataId);
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
			result = new WDCReader(fileToOpen->fullname());
		else if (strncmp(header, "WDC3", 4) == 0)
			result = new WDCReader(fileToOpen->fullname());
		else if (strncmp(header, "WDC4", 4) == 0)
			result = new WDCReader(fileToOpen->fullname());
		else if (strncmp(header, "WDC5", 4) == 0)
			result = new WDCReader(fileToOpen->fullname());
		else
			LOG_ERROR << "Unsupported database file" << header[0] << header[1] << header[2] << header[3];

		fileToOpen->close();
	}

	return result;
}
