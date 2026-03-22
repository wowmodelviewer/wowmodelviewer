#include "GameDatabase.h"
#include "Logger.h"
#include "Game.h"
#include "DBDFile.h"
#include "string_utils.h"
#include "HttpClient.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

static std::string dbdTypeToSqlType(const std::string& baseType, const std::string& sizeStr);

core::GameDatabase::~GameDatabase()
{
}

core::GameDatabase::GameDatabase() : m_fastMode(false)
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

core::TableStructure* core::GameDatabase::buildTableStructure(const std::string& tableName)
{
	namespace fs = std::filesystem;

	std::string dbdPath = m_dbdDir + "/" + tableName + ".dbd";

	if (!fs::exists(dbdPath) && !m_dbdBaseUrl.empty())
	{
		std::string url = m_dbdBaseUrl;
		auto pos = url.find("%s");
		if (pos != std::string::npos)
			url.replace(pos, 2, tableName);
		else
			url += tableName + ".dbd";

		LOG_INFO << "Downloading DBD for" << tableName << "from" << url;
		const auto resp = HttpClient::Get(url);
		if (resp.success && !resp.body.empty())
		{
			fs::create_directories(m_dbdDir);
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
		return nullptr;
	}

	core::DBDFile dbd;
	if (!dbd.parse(dbdPath))
	{
		LOG_ERROR << "Failed to parse DBD file:" << dbdPath;
		return nullptr;
	}

	const std::string layoutHash = getLayoutHashForTable(tableName);
	const core::DBDVersionDef* verDef = dbd.findVersion(m_build, layoutHash);
	if (!verDef)
	{
		LOG_ERROR << "No matching version definition found in" << dbdPath
				  << "for build" << m_build.major << "." << m_build.minor
				  << "." << m_build.patch << "." << m_build.build;
		return nullptr;
	}

	core::TableStructure* tblStruct = createTableStructure();
	tblStruct->name = tableName;
	tblStruct->file = tableName;

	readSpecificTableAttributesFromDBD(*verDef, tblStruct);

	int fieldId = 0;
	int fieldPos = 0;

	for (const auto& vField : verDef->fields)
	{
		const core::DBDColumnDef* colDef = dbd.findColumn(vField.name);
		if (!colDef)
		{
			LOG_ERROR << "Column definition not found for field" << vField.name
					  << "in table" << tableName;
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

		readSpecificFieldAttributesFromDBD(vField, *colDef, fieldStruct);

		if (!vField.isNonInline)
		{
			setFieldPos(fieldStruct, fieldPos);
			fieldPos++;
		}

		tblStruct->fields.push_back(fieldStruct);
	}

	return tblStruct;
}

core::TableStructure::~TableStructure()
{
	for (const auto it : fields)
		delete it;
}

// --- DBD support ---

bool core::GameDatabase::initFromDBD(const std::string& dbdDir, const std::string& buildVersion)
{
	return initFromDBD(dbdDir, buildVersion, {});
}

bool core::GameDatabase::initFromDBD(const std::string& dbdDir, const std::string& buildVersion,
									 const std::vector<std::string>& tableNames)
{
	m_dbdDir = dbdDir;
	m_build = core::DBDBuild::fromString(buildVersion);

	LOG_INFO << "Database initialized for on-demand loading (build "
			 << m_build.major << "." << m_build.minor << "."
			 << m_build.patch << "." << m_build.build << ")";
	return true;
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
