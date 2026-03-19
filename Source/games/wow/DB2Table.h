#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

class WDCReader;

struct DB2FieldInfo
{
	std::string name;
	std::string type;      // "text", "float", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "int", "byte"
	int pos = -1;          // DB2 field position index (-1 for non-inline/key)
	unsigned int arraySize = 1;
	bool isKey = false;
	bool isRelationshipData = false;
};

class DB2Table;

class DB2Row
{
public:
	DB2Row() = default;
	DB2Row(const DB2Table* table, size_t recordIndex)
		: m_table(table), m_recordIndex(recordIndex) {}

	bool isValid() const { return m_table != nullptr; }
	explicit operator bool() const { return isValid(); }

	uint32_t recordID() const;

	uint32_t getUInt(const std::string& field, unsigned int arrayIndex = 0) const;
	int32_t getInt(const std::string& field, unsigned int arrayIndex = 0) const;
	float getFloat(const std::string& field, unsigned int arrayIndex = 0) const;
	std::string getString(const std::string& field, unsigned int arrayIndex = 0) const;

private:
	const DB2Table* m_table = nullptr;
	size_t m_recordIndex = SIZE_MAX;
};

class DB2Table
{
public:
	DB2Table(WDCReader* reader, std::vector<DB2FieldInfo> fields);
	~DB2Table();

	DB2Table(const DB2Table&) = delete;
	DB2Table& operator=(const DB2Table&) = delete;

	// Lookup by primary key ID
	DB2Row getRow(uint32_t id) const;

	// Linear access by index (0-based into the record locations array)
	DB2Row getRowByIndex(size_t index) const;
	size_t getRowCount() const;

	// Range-based for support
	class Iterator
	{
	public:
		Iterator(const DB2Table* table, size_t index) : m_table(table), m_index(index) {}
		DB2Row operator*() const { return DB2Row(m_table, m_index); }
		Iterator& operator++() { ++m_index; return *this; }
		bool operator!=(const Iterator& other) const { return m_index != other.m_index; }
	private:
		const DB2Table* m_table;
		size_t m_index;
	};

	Iterator begin() const { return Iterator(this, 0); }
	Iterator end() const { return Iterator(this, getRowCount()); }

	// Field info lookup by base name
	const DB2FieldInfo* findField(const std::string& name) const;

private:
	friend class DB2Row;

	// Resolve a field name, handling SQL-style expanded array names (e.g. "Field1" -> "Field", arrayIndex=0).
	// arrayIndex is both input (caller-specified) and output (overwritten for expanded names).
	const DB2FieldInfo* resolveField(const std::string& name, unsigned int& arrayIndex) const;

	// Read a raw uint32 value for a given field from a record.
	uint32_t readUInt(size_t recordIndex, const DB2FieldInfo& field, unsigned int arrayIndex) const;

	// Read a string value for a given field from a record.
	std::string readString(size_t recordIndex, const DB2FieldInfo& field, unsigned int arrayIndex) const;

	// Get the record ID for a given record index.
	uint32_t getRecordID(size_t recordIndex) const;

	WDCReader* m_reader;  // owned
	std::vector<DB2FieldInfo> m_fields;
	std::unordered_map<std::string, size_t> m_fieldNameToIndex;  // base name -> index in m_fields
};
