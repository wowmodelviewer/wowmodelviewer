#include "DB2Table.h"
#include "WDCReader.h"
#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <cstring>

// ── DB2Row ───────────────────────────────────────────────────────────────────

uint32_t DB2Row::recordID() const
{
	if (!m_table) return 0;
	return m_table->getRecordID(m_recordIndex);
}

uint32_t DB2Row::getUInt(const std::string& field, unsigned int arrayIndex) const
{
	if (!m_table) return 0;
	unsigned int idx = arrayIndex;
	const DB2FieldInfo* fi = m_table->resolveField(field, idx);
	if (!fi) return 0;
	return m_table->readUInt(m_recordIndex, *fi, idx);
}

int32_t DB2Row::getInt(const std::string& field, unsigned int arrayIndex) const
{
	if (!m_table) return 0;
	unsigned int idx = arrayIndex;
	const DB2FieldInfo* fi = m_table->resolveField(field, idx);
	if (!fi) return 0;

	uint32_t raw = m_table->readUInt(m_recordIndex, *fi, idx);

	// Sign-extend based on the underlying type
	if (fi->type == "int8")
	{
		int8_t v;
		std::memcpy(&v, &raw, sizeof(int8_t));
		return static_cast<int32_t>(v);
	}
	if (fi->type == "int16")
	{
		int16_t v;
		std::memcpy(&v, &raw, sizeof(int16_t));
		return static_cast<int32_t>(v);
	}

	int32_t v;
	std::memcpy(&v, &raw, sizeof(int32_t));
	return v;
}

float DB2Row::getFloat(const std::string& field, unsigned int arrayIndex) const
{
	if (!m_table) return 0.0f;
	unsigned int idx = arrayIndex;
	const DB2FieldInfo* fi = m_table->resolveField(field, idx);
	if (!fi) return 0.0f;

	uint32_t raw = m_table->readUInt(m_recordIndex, *fi, idx);
	float result;
	std::memcpy(&result, &raw, sizeof(float));
	return result;
}

std::string DB2Row::getString(const std::string& field, unsigned int arrayIndex) const
{
	if (!m_table) return "";
	unsigned int idx = arrayIndex;
	const DB2FieldInfo* fi = m_table->resolveField(field, idx);
	if (!fi) return "";
	return m_table->readString(m_recordIndex, *fi, idx);
}

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string toLower(const std::string& s)
{
	std::string result = s;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

// ── DB2Table ─────────────────────────────────────────────────────────────────

DB2Table::DB2Table(WDCReader* reader, std::vector<DB2FieldInfo> fields)
	: m_reader(reader), m_fields(std::move(fields))
{
	for (size_t i = 0; i < m_fields.size(); i++)
		m_fieldNameToIndex[toLower(m_fields[i].name)] = i;
}

DB2Table::~DB2Table()
{
	delete m_reader;
}

DB2Row DB2Table::getRow(uint32_t id) const
{
	auto it = m_reader->m_idToRecordIndex.find(id);
	if (it != m_reader->m_idToRecordIndex.end())
		return DB2Row(this, it->second);
	return DB2Row();
}

DB2Row DB2Table::getRowByIndex(size_t index) const
{
	if (index < m_reader->m_recordLocations.size())
		return DB2Row(this, index);
	return DB2Row();
}

size_t DB2Table::getRowCount() const
{
	return m_reader->m_recordLocations.size();
}

const DB2FieldInfo* DB2Table::findField(const std::string& name) const
{
	auto it = m_fieldNameToIndex.find(toLower(name));
	if (it != m_fieldNameToIndex.end())
		return &m_fields[it->second];
	return nullptr;
}

const DB2FieldInfo* DB2Table::resolveField(const std::string& name, unsigned int& arrayIndex) const
{
	const std::string lower = toLower(name);

	// Try exact match first (base name or single field)
	auto it = m_fieldNameToIndex.find(lower);
	if (it != m_fieldNameToIndex.end())
		return &m_fields[it->second];

	// Try expanded SQL-style array name: "Field1" -> "Field" + arrayIndex=0
	size_t numStart = lower.size();
	while (numStart > 0 && std::isdigit(static_cast<unsigned char>(lower[numStart - 1])))
		--numStart;

	if (numStart > 0 && numStart < lower.size())
	{
		std::string baseName = lower.substr(0, numStart);
		it = m_fieldNameToIndex.find(baseName);
		if (it != m_fieldNameToIndex.end() && m_fields[it->second].arraySize > 1)
		{
			int idx = std::stoi(lower.substr(numStart)) - 1; // SQL names are 1-based
			if (idx >= 0 && static_cast<unsigned int>(idx) < m_fields[it->second].arraySize)
			{
				arrayIndex = static_cast<unsigned int>(idx);
				return &m_fields[it->second];
			}
		}
	}

	LOG_ERROR << "DB2Table: field not found: " << name;
	return nullptr;
}

uint32_t DB2Table::getRecordID(size_t recordIndex) const
{
	return m_reader->m_recordLocations[recordIndex].recordID;
}

uint32_t DB2Table::readUInt(size_t recordIndex, const DB2FieldInfo& field, unsigned int arrayIndex) const
{
	const auto& loc = m_reader->m_recordLocations[recordIndex];
	const auto& sec = m_reader->m_sections[loc.sectionIndex];

	if (field.isKey)
		return loc.recordID;

	if (field.isRelationshipData)
	{
		auto relIt = sec.relationshipMap.find(loc.localIndex);
		return (relIt != sec.relationshipMap.end()) ? relIt->second : 0;
	}

	unsigned int result = 0;
	m_reader->readFieldValue(loc.sectionIndex, loc.localIndex,
							 field.pos, arrayIndex, field.arraySize,
							 loc.recordID, result);
	return result;
}

std::string DB2Table::readString(size_t recordIndex, const DB2FieldInfo& field, unsigned int arrayIndex) const
{
	const auto& loc = m_reader->m_recordLocations[recordIndex];
	const auto& sec = m_reader->m_sections[loc.sectionIndex];
	const unsigned char* recordOffset = m_reader->getRecordOffset(loc.sectionIndex, loc.localIndex);

	// Read the raw field value (string table offset for normal records)
	unsigned int val = 0;
	if (field.pos >= 0)
		m_reader->readFieldValue(loc.sectionIndex, loc.localIndex,
								 field.pos, arrayIndex, field.arraySize,
								 loc.recordID, val);

	const char* stringPtr = nullptr;

	if (!sec.isNormal)
	{
		// Sparse table: strings are inline; walk through fields to find offset
		const unsigned char* ptr = recordOffset;
		for (int f = 0; f <= field.pos && static_cast<size_t>(f) < m_fields.size(); f++)
		{
			if (m_fields[f].isKey || m_fields[f].isRelationshipData || m_fields[f].pos < 0)
				continue;
			if (m_fields[f].type == "uint64")
			{
				ptr += 8;
			}
			else
			{
				std::string v(reinterpret_cast<const char*>(ptr));
				ptr += v.size() + 1;
			}
		}
		stringPtr = reinterpret_cast<const char*>(ptr);
	}
	else if (m_reader->m_wdcVersion > 2)
	{
		// WDC3+: string table reference
		const uint32_t dataPos = m_reader->m_fieldStorageInfo[field.pos].field_offset_bits / 8;

		uint32_t outsideDataSize = 0;
		for (uint32_t s = 0; s < loc.sectionIndex; s++)
			outsideDataSize += m_reader->m_sections[s].recordDataSize;

		const int32_t absoluteRecordOfs =
			static_cast<int32_t>(loc.localIndex * m_reader->m_header.record_size) -
			static_cast<int32_t>(m_reader->m_header.record_count * m_reader->m_header.record_size);

		const int32_t stringTableIndex =
			static_cast<int32_t>(outsideDataSize) + absoluteRecordOfs +
			static_cast<int32_t>(dataPos) + static_cast<int32_t>(val);

		const unsigned char* strBase = nullptr;
		for (uint32_t s = 0; s < m_reader->m_sections.size(); s++)
		{
			const auto& strSec = m_reader->m_sections[s];
			const int32_t localOfs = stringTableIndex - static_cast<int32_t>(strSec.previousStringTableSize);
			if (localOfs >= 0 && static_cast<uint32_t>(localOfs) < strSec.stringTableSize)
			{
				strBase = m_reader->m_fileData + strSec.stringTableOffset + localOfs;
				break;
			}
		}

		if (strBase)
			stringPtr = reinterpret_cast<const char*>(strBase);
		else
			stringPtr = "";
	}
	else
	{
		// WDC2: strings inline in record data
		stringPtr = reinterpret_cast<const char*>(
			recordOffset + m_reader->m_fieldStorageInfo[field.pos].field_offset_bits / 8 + val -
			((m_reader->m_header.record_count - sec.recordCount) * m_reader->m_header.record_size));
	}

	std::string result(stringPtr ? stringPtr : "");
	std::replace(result.begin(), result.end(), '"', '\'');
	return result;
}
