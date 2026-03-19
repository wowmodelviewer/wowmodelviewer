#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#define _DBDFILE_API_

namespace core
{
	// Represents a build version: major.minor.patch.build
	struct _DBDFILE_API_ DBDBuild
	{
		int major = 0;
		int minor = 0;
		int patch = 0;
		int build = 0;

		static DBDBuild fromString(const std::string& s);

		bool operator<(const DBDBuild& o) const;
		bool operator<=(const DBDBuild& o) const;
		bool operator==(const DBDBuild& o) const;
		bool operator>=(const DBDBuild& o) const;
	};

	// Column definition from the COLUMNS section of a .dbd file
	struct _DBDFILE_API_ DBDColumnDef
	{
		std::string type;        // int, string, float, locstring
		std::string name;        // column name (may end with '?' for unverified)
		std::string foreignDb;   // foreign key DB (empty if none)
		std::string foreignCol;  // foreign key column (empty if none)
		bool verified = true;
	};

	// A single field entry within a version definition
	struct _DBDFILE_API_ DBDVersionField
	{
		std::string name;
		std::string sizeStr;       // e.g. "32", "u8", "16", etc. (empty for strings/floats)
		unsigned int arraySize = 1;
		bool isID = false;
		bool isRelation = false;
		bool isNonInline = false;
		std::string comment;
	};

	// A version definition block (one BUILD/LAYOUT section)
	struct _DBDFILE_API_ DBDVersionDef
	{
		std::vector<std::string> layoutHashes;
		std::vector<DBDBuild> builds;                    // exact builds
		std::vector<std::pair<DBDBuild, DBDBuild>> buildRanges; // build ranges
		std::string comment;
		std::vector<DBDVersionField> fields;

		bool matchesBuild(const DBDBuild& target) const;
	};

	// Parsed content of a single .dbd file
	class _DBDFILE_API_ DBDFile
	{
	public:
		bool parse(const std::string& filepath);
		bool parse(std::istream& stream);

		const std::string& tableName() const { return m_tableName; }
		const std::vector<DBDColumnDef>& columns() const { return m_columns; }
		const std::vector<DBDVersionDef>& versions() const { return m_versions; }

		// Find the version definition that matches the given build.
		// Returns nullptr if no match found.
		const DBDVersionDef* findVersion(const DBDBuild& build) const;

		// Find the column definition for a given field name.
		// Returns nullptr if not found.
		const DBDColumnDef* findColumn(const std::string& name) const;

	private:
		bool parseColumns(std::istream& stream, std::string& nextLine);
		bool parseVersionDef(std::istream& stream, const std::string& firstLine, std::string& nextLine);

		std::string m_tableName;
		std::vector<DBDColumnDef> m_columns;
		std::vector<DBDVersionDef> m_versions;
	};
}
