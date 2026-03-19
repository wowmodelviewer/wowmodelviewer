#include "DBDFile.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

#include "Logger.h"
#include "string_utils.h"

// --- DBDBuild ---

core::DBDBuild core::DBDBuild::fromString(const std::string& s)
{
	DBDBuild b;
	auto parts = core::split(s, '.');
	if (parts.size() >= 1) b.major = std::stoi(parts[0]);
	if (parts.size() >= 2) b.minor = std::stoi(parts[1]);
	if (parts.size() >= 3) b.patch = std::stoi(parts[2]);
	if (parts.size() >= 4) b.build = std::stoi(parts[3]);
	return b;
}

bool core::DBDBuild::operator<(const DBDBuild& o) const
{
	if (major != o.major) return major < o.major;
	if (minor != o.minor) return minor < o.minor;
	if (patch != o.patch) return patch < o.patch;
	return build < o.build;
}

bool core::DBDBuild::operator<=(const DBDBuild& o) const { return !(o < *this); }
bool core::DBDBuild::operator==(const DBDBuild& o) const
{
	return major == o.major && minor == o.minor && patch == o.patch && build == o.build;
}
bool core::DBDBuild::operator>=(const DBDBuild& o) const { return !(*this < o); }

// --- DBDVersionDef ---

bool core::DBDVersionDef::matchesBuild(const DBDBuild& target) const
{
	for (const auto& b : builds)
	{
		if (b == target)
			return true;
	}
	for (const auto& [lo, hi] : buildRanges)
	{
		if (target >= lo && target <= hi)
			return true;
	}
	return false;
}

bool core::DBDVersionDef::matchesLayoutHash(const std::string& layoutHash) const
{
	if (layoutHash.empty())
		return false;

	for (const auto& h : layoutHashes)
	{
		if (h == layoutHash)
			return true;
	}
	return false;
}

// --- DBDFile ---

static std::string trimWhitespace(const std::string& s)
{
	const auto start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	const auto end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

bool core::DBDFile::parse(const std::string& filepath)
{
	std::ifstream file(filepath);
	if (!file.is_open())
	{
		LOG_ERROR << "Failed to open DBD file:" << filepath;
		return false;
	}

	// Extract table name from filename (e.g. "Map.dbd" -> "Map")
	std::filesystem::path p(filepath);
	m_tableName = p.stem().string();

	return parse(file);
}

bool core::DBDFile::parse(std::istream& stream)
{
	m_columns.clear();
	m_versions.clear();

	std::string line;

	// Skip to COLUMNS section
	while (std::getline(stream, line))
	{
		line = trimWhitespace(line);
		if (line == "COLUMNS")
			break;
	}

	// Parse column definitions
	std::string nextLine;
	if (!parseColumns(stream, nextLine))
		return false;

	// Parse version definitions
	// nextLine might already contain the first BUILD or LAYOUT line
	if (!nextLine.empty())
	{
		std::string afterBlock;
		if (!parseVersionDef(stream, nextLine, afterBlock))
			return false;

		while (!afterBlock.empty())
		{
			std::string next;
			if (!parseVersionDef(stream, afterBlock, next))
				return false;
			afterBlock = next;
		}
	}

	// Continue reading remaining version definitions
	while (std::getline(stream, line))
	{
		line = trimWhitespace(line);
		if (line.empty())
			continue;

		std::string next;
		if (!parseVersionDef(stream, line, next))
			return false;

		while (!next.empty())
		{
			std::string afterBlock;
			if (!parseVersionDef(stream, next, afterBlock))
				return false;
			next = afterBlock;
		}
	}

	return true;
}

bool core::DBDFile::parseColumns(std::istream& stream, std::string& nextLine)
{
	nextLine.clear();
	std::string line;

	while (std::getline(stream, line))
	{
		line = trimWhitespace(line);

		// Empty line marks end of COLUMNS section
		if (line.empty())
			continue;

		// If we hit a BUILD or LAYOUT line, we're past the columns section
		if (line.starts_with("BUILD") || line.starts_with("LAYOUT"))
		{
			nextLine = line;
			return true;
		}

		// Strip comments
		std::string comment;
		auto commentPos = line.find("//");
		if (commentPos != std::string::npos)
		{
			comment = trimWhitespace(line.substr(commentPos + 2));
			line = trimWhitespace(line.substr(0, commentPos));
		}

		if (line.empty())
			continue;

		DBDColumnDef col;

		// Check for foreign key: type<ForeignDB::ForeignCol> ColName
		auto angleBracketStart = line.find('<');
		auto angleBracketEnd = line.find('>');

		if (angleBracketStart != std::string::npos && angleBracketEnd != std::string::npos
			&& angleBracketEnd > angleBracketStart)
		{
			col.type = trimWhitespace(line.substr(0, angleBracketStart));
			std::string foreignRef = line.substr(angleBracketStart + 1, angleBracketEnd - angleBracketStart - 1);

			auto colonPos = foreignRef.find("::");
			if (colonPos != std::string::npos)
			{
				col.foreignDb = foreignRef.substr(0, colonPos);
				col.foreignCol = foreignRef.substr(colonPos + 2);
			}

			std::string remainder = trimWhitespace(line.substr(angleBracketEnd + 1));
			col.name = remainder;
		}
		else
		{
			// Regular: type ColName
			auto spacePos = line.find(' ');
			if (spacePos == std::string::npos)
				continue;

			col.type = line.substr(0, spacePos);
			col.name = trimWhitespace(line.substr(spacePos + 1));
		}

		// Check for unverified marker
		if (!col.name.empty() && col.name.back() == '?')
		{
			col.verified = false;
			col.name.pop_back();
		}

		m_columns.push_back(col);
	}

	return true;
}

bool core::DBDFile::parseVersionDef(std::istream& stream, const std::string& firstLine, std::string& nextLine)
{
	nextLine.clear();
	DBDVersionDef verDef;

	auto processHeaderLine = [&](const std::string& line)
	{
		if (line.starts_with("LAYOUT "))
		{
			std::string hashList = trimWhitespace(line.substr(7));
			auto hashes = core::split(hashList, ',');
			for (auto& h : hashes)
				verDef.layoutHashes.push_back(trimWhitespace(h));
		}
		else if (line.starts_with("BUILD "))
		{
			std::string buildStr = trimWhitespace(line.substr(6));

			// Split by comma first, then check each token for range or exact build.
			// This correctly handles mixed lines like:
			//   BUILD 0.5.3.3368-0.5.5.3494, 0.6.0.3592
			auto buildList = core::split(buildStr, ',');
			for (auto& token : buildList)
			{
				token = trimWhitespace(token);
				if (token.empty())
					continue;

				auto dashPos = token.find('-');
				if (dashPos != std::string::npos)
				{
					auto lo = DBDBuild::fromString(token.substr(0, dashPos));
					auto hi = DBDBuild::fromString(token.substr(dashPos + 1));
					verDef.buildRanges.emplace_back(lo, hi);
				}
				else
				{
					verDef.builds.push_back(DBDBuild::fromString(token));
				}
			}
		}
		else if (line.starts_with("COMMENT "))
		{
			verDef.comment = trimWhitespace(line.substr(8));
		}
	};

	// Process the first line
	processHeaderLine(firstLine);

	std::string line;
	bool inHeader = true;

	while (std::getline(stream, line))
	{
		line = trimWhitespace(line);

		// Empty line indicates end of this version block
		if (line.empty())
		{
			if (!verDef.fields.empty() || !verDef.builds.empty() || !verDef.buildRanges.empty())
				break;
			continue;
		}

		// Check if this is a header line (BUILD, LAYOUT, COMMENT)
		if (inHeader && (line.starts_with("BUILD ") || line.starts_with("LAYOUT ") || line.starts_with("COMMENT ")))
		{
			processHeaderLine(line);
			continue;
		}

		inHeader = false;

		// This must be a field definition line
		DBDVersionField field;

		// Strip comments
		auto commentPos = line.find("//");
		if (commentPos != std::string::npos)
		{
			field.comment = trimWhitespace(line.substr(commentPos + 2));
			line = trimWhitespace(line.substr(0, commentPos));
		}

		if (line.empty())
			continue;

		// Parse annotations: $annotation1,annotation2$
		if (line.starts_with("$"))
		{
			auto endAnnotation = line.find('$', 1);
			if (endAnnotation != std::string::npos)
			{
				std::string annotations = line.substr(1, endAnnotation - 1);
				auto parts = core::split(annotations, ',');
				for (const auto& p : parts)
				{
					std::string trimmed = trimWhitespace(p);
					if (trimmed == "id")
						field.isID = true;
					else if (trimmed == "relation")
						field.isRelation = true;
					else if (trimmed == "noninline")
						field.isNonInline = true;
				}
				line = trimWhitespace(line.substr(endAnnotation + 1));
			}
		}

		// Parse name, size, and array length
		// Possible formats: ColName, ColName<32>, ColName[5], ColName<32>[5]
		std::string fieldStr = line;

		// Extract array size [N]
		auto bracketStart = fieldStr.find('[');
		auto bracketEnd = fieldStr.find(']');
		if (bracketStart != std::string::npos && bracketEnd != std::string::npos)
		{
			std::string arraySizeStr = fieldStr.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
			field.arraySize = static_cast<unsigned int>(std::stoul(arraySizeStr));
			fieldStr = fieldStr.substr(0, bracketStart);
		}

		// Extract size <N>
		auto angleStart = fieldStr.find('<');
		auto angleEnd = fieldStr.find('>');
		if (angleStart != std::string::npos && angleEnd != std::string::npos)
		{
			field.sizeStr = fieldStr.substr(angleStart + 1, angleEnd - angleStart - 1);
			fieldStr = fieldStr.substr(0, angleStart);
		}

		field.name = trimWhitespace(fieldStr);
		verDef.fields.push_back(field);
	}

	if (!verDef.fields.empty() || !verDef.builds.empty() || !verDef.buildRanges.empty())
		m_versions.push_back(verDef);

	return true;
}

const core::DBDVersionDef* core::DBDFile::findVersion(const DBDBuild& build) const
{
	for (const auto& v : m_versions)
	{
		if (v.matchesBuild(build))
			return &v;
	}
	return nullptr;
}

const core::DBDVersionDef* core::DBDFile::findVersion(const DBDBuild& build, const std::string& layoutHash) const
{
	// Check layout hash first (most reliable match)
	if (!layoutHash.empty())
	{
		for (const auto& v : m_versions)
		{
			if (v.matchesLayoutHash(layoutHash))
				return &v;
		}
	}

	// Fall back to build matching
	return findVersion(build);
}

const core::DBDColumnDef* core::DBDFile::findColumn(const std::string& name) const
{
	for (const auto& c : m_columns)
	{
		if (c.name == name)
			return &c;
	}
	return nullptr;
}
