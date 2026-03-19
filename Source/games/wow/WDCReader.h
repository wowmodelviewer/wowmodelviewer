#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>

#include "dbfile.h"
#include "CASCFile.h"

#define _WDCREADER_API_

// Unified WDC reader for WDC2, WDC3, WDC4 and WDC5 DB2 files.
// Modelled after wow.export's WDCReader.js — supports multiple sections,
// encrypted-section detection, 64-bit bitpacked reads, and WDC4/5 extras.
class _WDCREADER_API_ WDCReader : public DBFile, public CASCFile
{
public:
	explicit WDCReader(const std::string& file);
	~WDCReader() override;

	bool open() override;
	bool close() override;

	std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure* structure) const override;

private:
	friend class DB2Table;

	// ── compression types (same enum values as the on-disk format) ────────
	enum FieldCompression : uint32_t
	{
		COMP_NONE = 0,
		COMP_BITPACKED = 1,
		COMP_COMMON_DATA = 2,
		COMP_BITPACKED_INDEXED = 3,
		COMP_BITPACKED_INDEXED_ARRAY = 4,
		COMP_BITPACKED_SIGNED = 5
	};

	// ── on-disk structures ───────────────────────────────────────────────
#pragma pack(push, 1)
	struct WDCHeader
	{
		char     magic[4];
		uint32_t record_count;
		uint32_t field_count;
		uint32_t record_size;
		uint32_t string_table_size;
		uint32_t table_hash;
		uint32_t layout_hash;
		uint32_t min_id;
		uint32_t max_id;
		uint32_t locale;
		uint16_t flags;
		uint16_t id_index;
		uint32_t total_field_count;
		uint32_t bitpacked_data_offset;
		uint32_t lookup_column_count;
		uint32_t field_storage_info_size;
		uint32_t common_data_size;
		uint32_t pallet_data_size;
		uint32_t section_count;
	};

	struct SectionHeaderWDC2
	{
		uint64_t tact_key_hash;
		uint32_t file_offset;
		uint32_t record_count;
		uint32_t string_table_size;
		uint32_t copy_table_size;
		uint32_t offset_map_offset;
		uint32_t id_list_size;
		uint32_t relationship_data_size;
	};

	struct SectionHeaderWDC3
	{
		uint64_t tact_key_hash;
		uint32_t file_offset;
		uint32_t record_count;
		uint32_t string_table_size;
		uint32_t offset_records_end;
		uint32_t id_list_size;
		uint32_t relationship_data_size;
		uint32_t offset_map_id_count;
		uint32_t copy_table_count;
	};

	struct FieldStructure
	{
		int16_t  size;
		uint16_t position;
	};

	struct FieldStorageInfo
	{
		uint16_t field_offset_bits;
		uint16_t field_size_bits;
		uint32_t additional_data_size;
		uint32_t storage_type;
		uint32_t val1;
		uint32_t val2;
		uint32_t val3;
	};

	struct OffsetMapEntry
	{
		uint32_t offset;
		uint16_t size;
	};

	struct CopyTableEntry
	{
		uint32_t newRowId;
		uint32_t copiedRowId;
	};
#pragma pack(pop)

	// ── per-section runtime data ─────────────────────────────────────────
	struct SectionData
	{
		// normalised header values (valid for both WDC2 and WDC3+)
		uint64_t tactKeyHash = 0;
		uint32_t fileOffset = 0;
		uint32_t recordCount = 0;
		uint32_t stringTableSize = 0;
		uint32_t offsetRecordsEnd = 0;   // WDC3+; for WDC2 = offsetMapOffset
		uint32_t idListSize = 0;
		uint32_t relationshipDataSize = 0;
		uint32_t offsetMapIdCount = 0;   // WDC3+ only
		uint32_t copyTableCount = 0;     // WDC3+ (for WDC2 = copyTableSize / 8)

		bool     isNormal = true;        // !(flags & 0x01)
		bool     isEncrypted = false;

		// pointers into m_fileData
		unsigned char* recordDataPtr = nullptr;
		uint32_t       recordDataSize = 0;
		uint32_t       stringTableOffset = 0; // absolute position of this section's string block
		uint32_t       previousStringTableSize = 0; // cumulative string table size of earlier sections

		std::vector<uint32_t> idList;
		std::vector<OffsetMapEntry> offsetMap;
		std::map<uint32_t, uint32_t> relationshipMap; // recordIndex -> foreignID
	};

	// ── reading helpers ──────────────────────────────────────────────────
	uint64_t readBitpackedValue64(const FieldStorageInfo& info, const unsigned char* recordOffset) const;

	bool readFieldValue(unsigned int sectionIndex, unsigned int recordIndexInSection,
						unsigned int fieldIndex, unsigned int arrayIndex, unsigned int arraySize,
						uint32_t recordID, unsigned int& result) const;

	const unsigned char* getRecordOffset(unsigned int sectionIndex, unsigned int recordIndexInSection) const;

	// ── member data ──────────────────────────────────────────────────────
	int m_wdcVersion = 0;

	WDCHeader m_header{};

	std::vector<FieldStorageInfo>              m_fieldStorageInfo;
	std::map<uint32_t, uint32_t>               m_palletBlockOffsets;
	unsigned char*                             m_palletData = nullptr;
	std::map<uint32_t, std::map<uint32_t, uint32_t>> m_commonData;

	std::vector<SectionData> m_sections;

	// flat index that maps a linear recordIndex (0..totalRecords-1) used by the
	// DBFile iterator to (sectionIndex, recordIndexInSection, recordID).
	struct RecordLocation
	{
		uint32_t sectionIndex;
		uint32_t localIndex;
		uint32_t recordID;
	};
	std::vector<RecordLocation> m_recordLocations;

	// recordID -> index in m_recordLocations for O(1) getRow() by ID
	std::unordered_map<uint32_t, size_t> m_idToRecordIndex;

	// relationship reverse lookup: foreignID -> list of recordIDs
	std::unordered_map<uint32_t, std::vector<uint32_t>> m_relationshipLookup;

	// copy table: destID -> srcID
	std::map<uint32_t, uint32_t> m_copyTable;

	// keep the raw file data alive for the lifetime of the reader so that
	// record-offset pointers remain valid.
	unsigned char* m_fileData = nullptr;
	size_t         m_fileDataSize = 0;

	std::map<int, int> m_fieldSizes;
};
