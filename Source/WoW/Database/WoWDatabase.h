#pragma once

#include "GameDatabase.h"

#include <memory>
#include <string>
#include <unordered_map>

class GameFile;
class DB2Table;

namespace wow
{
	class TableStructure : public core::TableStructure
	{
	public:
		TableStructure() : hash(0)
		{
		}

		unsigned int hash;
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

	/// @brief WoW-specific database that lazy-loads DB2 tables from the CASC archive.
	class WoWDatabase : public core::GameDatabase
	{
	public:
		WoWDatabase();
		~WoWDatabase() override;

		// Direct DB2 table access (lazy-loaded, on-demand like wow.export).
		// Returns nullptr if the table cannot be loaded.
		const DB2Table* getTable(const std::string& name);

		core::TableStructure* createTableStructure();
		core::FieldStructure* createFieldStructure();

		void readSpecificTableAttributesFromDBD(const core::DBDVersionDef&, core::TableStructure*) override;
		void readSpecificFieldAttributesFromDBD(const core::DBDVersionField&, const core::DBDColumnDef&,
											core::FieldStructure*) override;
		void setFieldPos(core::FieldStructure*, int pos) override;
		std::string getLayoutHashForTable(const std::string& tableName) override;

	private:
		std::unique_ptr<DB2Table> buildDB2Table(const std::string& tableName);

		std::unordered_map<std::string, std::unique_ptr<DB2Table>> m_tables;
	};
}

// Convenience macro: access the WoWDatabase instance.
// Requires Game.h to be included for the GAMEDATABASE macro.
#define WOWDB static_cast<wow::WoWDatabase&>(GAMEDATABASE)
