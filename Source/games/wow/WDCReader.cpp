#include "WDCReader.h"
#include "logger/Logger.h"
#include "Game.h"
#include "WoWDatabase.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#define WDC_READ_DEBUG 1

// ── magic constants ──────────────────────────────────────────────────────────
static constexpr uint32_t MAGIC_WDC2 = 0x32434457; // 'WDC2'
static constexpr uint32_t MAGIC_CLS1 = 0x434C5331; // 'CLS1' (alias for WDC2)
static constexpr uint32_t MAGIC_WDC3 = 0x33434457; // 'WDC3'
static constexpr uint32_t MAGIC_WDC4 = 0x34434457; // 'WDC4'
static constexpr uint32_t MAGIC_WDC5 = 0x35434457; // 'WDC5'

// ── helpers ──────────────────────────────────────────────────────────────────
static int versionFromMagic(uint32_t magic)
{
	switch (magic)
	{
	case MAGIC_WDC2: return 2;
	case MAGIC_CLS1: return 2;
	case MAGIC_WDC3: return 3;
	case MAGIC_WDC4: return 4;
	case MAGIC_WDC5: return 5;
	default: return 0;
	}
}

// ─────────────────────────────────────────────────────────────────────────────

WDCReader::WDCReader(const std::string& file)
	: CASCFile(file)
{
}

WDCReader::~WDCReader()
{
	WDCReader::close();
	delete[] m_palletData;
	// m_fileData is the CASCFile buffer — do NOT delete it here;
	// CASCFile::close() handles the buffer.
}

bool WDCReader::close()
{
	m_fileData = nullptr;
	m_fileDataSize = 0;
	return CASCFile::close();
}

// ─────────────────────────────────────────────────────────────────────────────
bool WDCReader::open()
{
	// Must use memory-buffered mode (true) because WDCReader stores direct
	// pointers into the file data for random access during get() calls.
	// open(false) streams via CascReadFile and getBuffer() returns nullptr.
	if (!CASCFile::open(true))
	{
		LOG_ERROR << "WDCReader: failed to open file " << fullname();
		return false;
	}

	m_fileData = getBuffer();
	m_fileDataSize = getSize();

	if (m_fileDataSize < sizeof(WDCHeader))
	{
		LOG_ERROR << "WDCReader: file too small " << fullname();
		return false;
	}

	// ── 1. Read magic and determine version ──────────────────────────────
	uint32_t magic = 0;
	read(&magic, 4);

	m_wdcVersion = versionFromMagic(magic);
	if (m_wdcVersion == 0)
	{
		LOG_ERROR << "WDCReader: unsupported magic in " << fullname();
		return false;
	}

	// WDC5 inserts versionNum(4) + schemaString(128) between magic and the
	// rest of the header.
	if (m_wdcVersion == 5)
	{
		seek(4); // already past magic
		uint32_t versionNum;
		read(&versionNum, 4);
		char schemaString[128];
		read(schemaString, 128);
	}

	// Read the common header (record_count onward)
	// We already consumed magic (and the WDC5 extra), so read the rest.
	const size_t headerRemainder = sizeof(WDCHeader) - sizeof(m_header.magic);
	read(&m_header.record_count, headerRemainder);
	std::memcpy(m_header.magic, &magic, 4);

	recordSize  = m_header.record_size;
	recordCount = m_header.record_count;
	fieldCount  = m_header.field_count;
	stringSize  = m_header.string_table_size;

#if WDC_READ_DEBUG > 0
	LOG_INFO << "WDCReader:" << fullname() << "version=" << m_wdcVersion
			 << "records=" << m_header.record_count
			 << "fields=" << m_header.field_count
			 << "recordSize=" << m_header.record_size
			 << "sections=" << m_header.section_count
			 << "flags=0x" << std::hex << m_header.flags << std::dec;
#endif

	LOG_INFO << "WDCReader step 1 done: version=" << m_wdcVersion
			 << " fileSize=" << m_fileDataSize << " pos=" << getPos()
			 << " for " << fullname();

	// ── 2. Section headers ───────────────────────────────────────────────
	const uint32_t sectionCount = m_header.section_count;
	m_sections.resize(sectionCount);

	for (uint32_t i = 0; i < sectionCount; i++)
	{
		SectionData& sec = m_sections[i];
		sec.isNormal = !(m_header.flags & 0x01);

		if (m_wdcVersion == 2)
		{
			SectionHeaderWDC2 sh{};
			read(&sh, sizeof(sh));
			sec.tactKeyHash          = sh.tact_key_hash;
			sec.fileOffset           = sh.file_offset;
			sec.recordCount          = sh.record_count;
			sec.stringTableSize      = sh.string_table_size;
			sec.offsetRecordsEnd     = sh.offset_map_offset;
			sec.idListSize           = sh.id_list_size;
			sec.relationshipDataSize = sh.relationship_data_size;
			sec.offsetMapIdCount     = 0;
			sec.copyTableCount       = sh.copy_table_size / 8;
		}
		else
		{
			SectionHeaderWDC3 sh{};
			read(&sh, sizeof(sh));
			sec.tactKeyHash          = sh.tact_key_hash;
			sec.fileOffset           = sh.file_offset;
			sec.recordCount          = sh.record_count;
			sec.stringTableSize      = sh.string_table_size;
			sec.offsetRecordsEnd     = sh.offset_records_end;
			sec.idListSize           = sh.id_list_size;
			sec.relationshipDataSize = sh.relationship_data_size;
			sec.offsetMapIdCount     = sh.offset_map_id_count;
			sec.copyTableCount       = sh.copy_table_count;
		}
	}

	LOG_INFO << "WDCReader step 2 done: sectionCount=" << sectionCount
			 << " pos=" << getPos() << " for " << fullname();

	// ── 3. Field structures ──────────────────────────────────────────────
	{
		std::vector<FieldStructure> fields(m_header.total_field_count);
		read(fields.data(), m_header.total_field_count * sizeof(FieldStructure));
		for (uint32_t i = 0; i < m_header.total_field_count; i++)
			m_fieldSizes[fields[i].position] = fields[i].size;
	}

	LOG_INFO << "WDCReader step 3 done: totalFieldCount=" << m_header.total_field_count
			 << " pos=" << getPos() << " for " << fullname();

	// ── 4. Field storage info ────────────────────────────────────────────
	if (m_header.field_storage_info_size > 0)
	{
		const uint32_t count = m_header.field_storage_info_size / sizeof(FieldStorageInfo);
		m_fieldStorageInfo.resize(count);
		read(m_fieldStorageInfo.data(), m_header.field_storage_info_size);
	}

	LOG_INFO << "WDCReader step 4 done: fieldStorageInfoSize=" << m_header.field_storage_info_size
			 << " pos=" << getPos() << " for " << fullname();

	// ── 5. Pallet data ──────────────────────────────────────────────────
	if (m_header.pallet_data_size > 0)
	{
		m_palletData = new unsigned char[m_header.pallet_data_size];
		read(m_palletData, m_header.pallet_data_size);

		uint32_t fieldId = 0;
		uint32_t offset = 0;
		for (auto& info : m_fieldStorageInfo)
		{
			if ((info.storage_type == COMP_BITPACKED_INDEXED ||
				 info.storage_type == COMP_BITPACKED_INDEXED_ARRAY) &&
				info.additional_data_size != 0)
			{
				m_palletBlockOffsets[fieldId] = offset;
				offset += info.additional_data_size;
			}
			fieldId++;
		}
	}

	LOG_INFO << "WDCReader step 5 done: palletDataSize=" << m_header.pallet_data_size
			 << " pos=" << getPos() << " for " << fullname();

	// ── 6. Common data ──────────────────────────────────────────────────
	if (m_header.common_data_size > 0)
	{
		unsigned char* commonRaw = new unsigned char[m_header.common_data_size];
		read(commonRaw, m_header.common_data_size);

		uint32_t fieldId = 0;
		size_t offset = 0;
		for (auto& info : m_fieldStorageInfo)
		{
			if (info.storage_type == COMP_COMMON_DATA && info.additional_data_size != 0)
			{
				unsigned char* ptr = commonRaw + offset;
				std::map<uint32_t, uint32_t> vals;
				for (uint32_t i = 0; i < info.additional_data_size / 8; i++)
				{
					uint32_t id, val;
					std::memcpy(&id, ptr, 4);  ptr += 4;
					std::memcpy(&val, ptr, 4); ptr += 4;
					vals[id] = val;
				}
				m_commonData[fieldId] = std::move(vals);
				offset += info.additional_data_size;
			}
			fieldId++;
		}
		delete[] commonRaw;
	}

	LOG_INFO << "WDCReader step 6 done: commonDataSize=" << m_header.common_data_size
			 << " pos=" << getPos() << " for " << fullname();

	// ── 7. WDC4+ inter-section data (skip) ──────────────────────────────
	if (m_wdcVersion > 3 && sectionCount > 1)
	{
		for (uint32_t si = 0; si < sectionCount - 1; si++)
		{
			uint32_t entryCount = 0;
			read(&entryCount, 4);
			seekRelative(entryCount * 4);
		}
	}

	LOG_INFO << "WDCReader step 7 done: pos=" << getPos()
			 << " eof=" << isEof() << " for " << fullname();

	// ── 8. Data sections ────────────────────────────────────────────────
	uint32_t previousStringTableSize = 0;

	for (uint32_t si = 0; si < sectionCount; si++)
	{
		SectionData& sec = m_sections[si];

		LOG_INFO << "WDCReader step 8: section " << si
				 << " recordCount=" << sec.recordCount
				 << " stringTableSize=" << sec.stringTableSize
				 << " idListSize=" << sec.idListSize
				 << " copyTableCount=" << sec.copyTableCount
				 << " relDataSize=" << sec.relationshipDataSize
				 << " offsetMapIdCount=" << sec.offsetMapIdCount
				 << " tactKey=" << sec.tactKeyHash
				 << " pos=" << getPos()
				 << " for " << fullname();

		const uint32_t recordDataOfs = static_cast<uint32_t>(getPos());
		const uint32_t recordDataSize = sec.isNormal
			? (m_header.record_size * sec.recordCount)
			: (sec.offsetRecordsEnd - sec.fileOffset);

		sec.recordDataPtr  = m_fileData + recordDataOfs;
		sec.recordDataSize = recordDataSize;

		// skip past record data
		seekRelative(recordDataSize);

		// string block
		sec.stringTableOffset = static_cast<uint32_t>(getPos());
		sec.previousStringTableSize = previousStringTableSize;
		if (m_wdcVersion > 2)
			previousStringTableSize += sec.stringTableSize;

		seekRelative(sec.stringTableSize);

		// id list
		if (sec.idListSize > 0)
		{
			const uint32_t count = sec.idListSize / 4;
			sec.idList.resize(count);
			read(sec.idList.data(), sec.idListSize);
		}

		// copy table
		if (sec.copyTableCount > 0)
		{
			for (uint32_t i = 0; i < sec.copyTableCount; i++)
			{
				CopyTableEntry entry{};
				read(&entry, sizeof(entry));
				if (entry.newRowId != entry.copiedRowId)
					m_copyTable[entry.newRowId] = entry.copiedRowId;
			}
		}

		// offset map (WDC3+)
		if (m_wdcVersion > 2 && sec.offsetMapIdCount > 0)
		{
			sec.offsetMap.resize(sec.offsetMapIdCount);
			read(sec.offsetMap.data(), sec.offsetMapIdCount * sizeof(OffsetMapEntry));
		}

		// WDC2 offset map (different location)
		if (m_wdcVersion == 2 && !sec.isNormal)
		{
			const uint32_t mapCount = m_header.max_id - m_header.min_id + 1;
			seek(sec.offsetRecordsEnd); // offsetMapOffset for WDC2
			sec.offsetMap.resize(mapCount);
			read(sec.offsetMap.data(), mapCount * sizeof(OffsetMapEntry));
		}

		// relationship map
		if (sec.relationshipDataSize > 0)
		{
			const size_t relStart = getPos();
			uint32_t numEntries = 0;
			read(&numEntries, 4);
			seekRelative(8); // skip min/max relationship ID

			for (uint32_t i = 0; i < numEntries; i++)
			{
				uint32_t foreignID, recIndex;
				read(&foreignID, 4);
				read(&recIndex, 4);
				sec.relationshipMap[recIndex] = foreignID;

				m_relationshipLookup[foreignID]; // ensure entry exists
			}

			// If section is encrypted we may have read the wrong amount — fix up
			const size_t expected = relStart + sec.relationshipDataSize;
			if (getPos() != expected)
				seek(expected);
		}

		// offset map id list (WDC3+ — duplicate of id list for offset records)
		if (m_wdcVersion > 2 && sec.offsetMapIdCount > 0)
			seekRelative(sec.offsetMapIdCount * 4);
	}

	LOG_INFO << "WDCReader step 8 done: data sections parsed, pos=" << getPos()
			 << " eof=" << isEof() << " for " << fullname();

	// ── 9. Detect encrypted sections & build record locations ───────────
	recordCount = 0;

	for (uint32_t si = 0; si < sectionCount; si++)
	{
		SectionData& sec = m_sections[si];

		LOG_INFO << "WDCReader step 9: section " << si
				 << " recordDataSize=" << sec.recordDataSize
				 << " tactKey=" << sec.tactKeyHash
				 << " for " << fullname();

		// Encrypted section detection
		if (sec.tactKeyHash != 0)
		{
			bool isZeroed = true;

			// check if record data is all zeroes
			for (uint32_t i = 0; i < sec.recordDataSize && isZeroed; i++)
			{
				if (sec.recordDataPtr[i] != 0x00)
					isZeroed = false;
			}

			// check if first integer after string block is non-zero
			if (isZeroed && m_wdcVersion > 2 && sec.isNormal &&
				(sec.idListSize > 0 || sec.copyTableCount > 0))
			{
				const unsigned char* ptr = m_fileData + sec.stringTableOffset + sec.stringTableSize;
				uint32_t firstVal = 0;
				std::memcpy(&firstVal, ptr, 4);
				isZeroed = (firstVal == 0);
			}

			// check first offset map entry
			if (isZeroed && m_wdcVersion > 2 && sec.offsetMapIdCount > 0)
				isZeroed = (sec.offsetMap[0].size == 0);

			if (isZeroed)
			{
				LOG_INFO << "WDCReader: skipping encrypted section " << si << " in " << fullname();
				sec.isEncrypted = true;
				continue;
			}
		}

		// Build record locations for this section
		const bool hasIDMap = !sec.idList.empty();
		const bool emptyIDMap = hasIDMap &&
			std::all_of(sec.idList.begin(), sec.idList.end(), [](uint32_t id) { return id == 0; });

		// If no ID list, read IDs from inline field
		if (!hasIDMap)
		{
			const FieldStorageInfo& idInfo = m_fieldStorageInfo[m_header.id_index];
			for (uint32_t i = 0; i < sec.recordCount; i++)
			{
				uint32_t id = 0;
				const unsigned char* recPtr = sec.recordDataPtr + (i * m_header.record_size);

				switch (idInfo.storage_type)
				{
				case COMP_NONE:
				{
					const uint32_t fieldSize = idInfo.field_size_bits / 8;
					std::memcpy(&id, recPtr + idInfo.field_offset_bits / 8, std::min(fieldSize, 4u));
					if (idInfo.field_size_bits < 32)
						id &= (1u << idInfo.field_size_bits) - 1;
					break;
				}
				case COMP_BITPACKED:
				case COMP_BITPACKED_SIGNED:
				{
					id = static_cast<uint32_t>(readBitpackedValue64(idInfo, recPtr));
					break;
				}
				default:
					LOG_ERROR << "WDCReader: unsupported ID compression type " << idInfo.storage_type;
					return false;
				}

				RecordLocation loc;
				loc.sectionIndex = si;
				loc.localIndex = i;
				loc.recordID = id;
				m_recordLocations.push_back(loc);
			}
		}
		else
		{
			for (uint32_t i = 0; i < sec.recordCount; i++)
			{
				RecordLocation loc;
				loc.sectionIndex = si;
				loc.localIndex = i;
				loc.recordID = (hasIDMap && !emptyIDMap) ? sec.idList[i] : i;
				m_recordLocations.push_back(loc);
			}
		}

		recordCount += sec.recordCount;
	}

	LOG_INFO << "WDCReader step 9 done: recordLocations=" << m_recordLocations.size()
			 << " for " << fullname();

	// ── 10. Inflate copy table ──────────────────────────────────────────
	// Build a recordID -> RecordLocation map for source lookup
	std::unordered_map<uint32_t, size_t> idToLocationIndex;
	for (size_t i = 0; i < m_recordLocations.size(); i++)
		idToLocationIndex[m_recordLocations[i].recordID] = i;

	for (const auto& [destID, srcID] : m_copyTable)
	{
		auto it = idToLocationIndex.find(srcID);
		if (it != idToLocationIndex.end())
		{
			RecordLocation loc = m_recordLocations[it->second];
			loc.recordID = destID;
			m_recordLocations.push_back(loc);
		}
	}

	recordCount = m_recordLocations.size();

	LOG_INFO << "WDCReader step 10 done: totalRecords=" << recordCount
			 << " copyTable=" << m_copyTable.size() << " for " << fullname();

	// Build ID-to-record-index lookup for O(1) getRow() by ID
	m_idToRecordIndex.clear();
	m_idToRecordIndex.reserve(m_recordLocations.size());
	for (size_t i = 0; i < m_recordLocations.size(); i++)
		m_idToRecordIndex[m_recordLocations[i].recordID] = i;

	LOG_INFO << "WDCReader step 11 done: idToRecordIndex built for " << fullname();

	// Populate relationship reverse lookup using a map for O(1) lookups
	{
		std::unordered_map<uint64_t, uint32_t> sectionLocalToRecordID;
		for (const auto& loc : m_recordLocations)
			sectionLocalToRecordID[(uint64_t(loc.sectionIndex) << 32) | loc.localIndex] = loc.recordID;

		for (uint32_t si = 0; si < sectionCount; si++)
		{
			const SectionData& sec = m_sections[si];
			for (const auto& [recIndex, foreignID] : sec.relationshipMap)
			{
				const uint64_t key = (uint64_t(si) << 32) | recIndex;
				auto it = sectionLocalToRecordID.find(key);
				if (it != sectionLocalToRecordID.end())
					m_relationshipLookup[foreignID].push_back(it->second);
			}
		}
	}

	LOG_INFO << "WDCReader: parsed " << fullname()
			 << " version=" << m_wdcVersion
			 << " records=" << recordCount
			 << " sections=" << sectionCount;

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
const unsigned char* WDCReader::getRecordOffset(unsigned int sectionIndex,
												unsigned int recordIndexInSection) const
{
	const SectionData& sec = m_sections[sectionIndex];
	if (sec.isNormal)
		return sec.recordDataPtr + (recordIndexInSection * m_header.record_size);

	// sparse / offset-map records
	if (m_wdcVersion == 2)
	{
		// WDC2: offset map is indexed by (recordID - min_id)
		// we need to find the recordID for this index
		const uint32_t recordID = sec.idList.empty()
			? recordIndexInSection
			: sec.idList[recordIndexInSection];
		const uint32_t mapIndex = recordID - m_header.min_id;
		if (mapIndex < sec.offsetMap.size())
			return m_fileData + sec.offsetMap[mapIndex].offset;
	}
	else
	{
		// WDC3+: offset map indexed by recordIndexInSection
		if (recordIndexInSection < sec.offsetMap.size())
			return m_fileData + sec.offsetMap[recordIndexInSection].offset;
	}

	return sec.recordDataPtr;
}

// ─────────────────────────────────────────────────────────────────────────────
uint64_t WDCReader::readBitpackedValue64(const FieldStorageInfo& info,
										 const unsigned char* recordOffset) const
{
	const uint32_t byteOffset = info.field_offset_bits / 8;
	const uint32_t numBytes = (info.field_size_bits + (info.field_offset_bits & 7) + 7) / 8;

	uint64_t raw = 0;
	std::memcpy(&raw, recordOffset + byteOffset, std::min(numBytes, 8u));

	raw >>= (info.field_offset_bits & 7);
	raw &= (1ull << info.field_size_bits) - 1;
	return raw;
}

// ─────────────────────────────────────────────────────────────────────────────
bool WDCReader::readFieldValue(unsigned int sectionIndex,
							   unsigned int recordIndexInSection,
							   unsigned int fieldIndex,
							   unsigned int arrayIndex,
							   unsigned int arraySize,
							   uint32_t recordID,
							   unsigned int& result) const
{
	const unsigned char* recordOffset = getRecordOffset(sectionIndex, recordIndexInSection);
	const FieldStorageInfo& info = m_fieldStorageInfo[fieldIndex];

	switch (info.storage_type)
	{
	case COMP_NONE:
	{
		uint32_t fieldSize = info.field_size_bits / 8;
		const unsigned char* fieldOffset = recordOffset + info.field_offset_bits / 8;

		if (arraySize != 1)
		{
			fieldSize /= arraySize;
			fieldOffset += ((info.field_size_bits / 8 / arraySize) * arrayIndex);
		}

		result = 0;
		std::memcpy(&result, fieldOffset, std::min(fieldSize, 4u));
		{
			const uint32_t bitsPerElement = info.field_size_bits / arraySize;
			if (bitsPerElement < 32)
				result &= (1u << bitsPerElement) - 1;
		}
		break;
	}
	case COMP_BITPACKED:
	case COMP_BITPACKED_SIGNED:
	{
		result = static_cast<uint32_t>(readBitpackedValue64(info, recordOffset));
		break;
	}
	case COMP_COMMON_DATA:
	{
		result = info.val1; // default value
		auto mapIt = m_commonData.find(fieldIndex);
		if (mapIt != m_commonData.end())
		{
			auto valIt = mapIt->second.find(recordID);
			if (valIt != mapIt->second.end())
				result = valIt->second;
		}
		break;
	}
	case COMP_BITPACKED_INDEXED:
	{
		const uint32_t index = static_cast<uint32_t>(readBitpackedValue64(info, recordOffset));
		auto it = m_palletBlockOffsets.find(fieldIndex);
		if (it == m_palletBlockOffsets.end())
		{
			LOG_ERROR << "WDCReader: missing pallet offset for field " << fieldIndex;
			return false;
		}
		const uint32_t offset = it->second + index * 4;
		std::memcpy(&result, m_palletData + offset, 4);
		break;
	}
	case COMP_BITPACKED_INDEXED_ARRAY:
	{
		const uint32_t index = static_cast<uint32_t>(readBitpackedValue64(info, recordOffset));
		auto it = m_palletBlockOffsets.find(fieldIndex);
		if (it == m_palletBlockOffsets.end())
		{
			LOG_ERROR << "WDCReader: missing pallet offset for field " << fieldIndex;
			return false;
		}
		const uint32_t offset = it->second + index * arraySize * 4 + arrayIndex * 4;
		std::memcpy(&result, m_palletData + offset, 4);
		break;
	}
	default:
		LOG_ERROR << "WDCReader: unsupported compression type " << info.storage_type;
		return false;
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> WDCReader::get(unsigned int recordIndex,
										const core::TableStructure* structure) const
{
	std::vector<std::string> result;

	if (recordIndex >= m_recordLocations.size())
		return result;

	const RecordLocation& loc = m_recordLocations[recordIndex];
	const SectionData& sec = m_sections[loc.sectionIndex];
	const unsigned char* recordOffset = getRecordOffset(loc.sectionIndex, loc.localIndex);

	for (auto it : structure->fields)
	{
		wow::FieldStructure* field = dynamic_cast<wow::FieldStructure*>(it);
		if (!field)
			continue;

		// ID field — read from the record location
		if (field->isKey)
		{
			std::stringstream ss;
			ss << loc.recordID;
			result.push_back(ss.str());
			continue;
		}

		// Relationship (non-inline foreign key)
		if (field->isRelationshipData)
		{
			auto relIt = sec.relationshipMap.find(loc.localIndex);
			if (relIt != sec.relationshipMap.end())
			{
				std::stringstream ss;
				ss << relIt->second;
				result.push_back(ss.str());
			}
			else
			{
				result.emplace_back("");
			}
			continue;
		}

		// Regular field — may be an array
		for (unsigned int i = 0; i < field->arraySize; i++)
		{
			unsigned int val = 0;
			if (!readFieldValue(loc.sectionIndex, loc.localIndex,
								field->pos, i, field->arraySize, loc.recordID, val))
				continue;

			if (field->type == "text")
			{
				const char* stringPtr = nullptr;
				if (!sec.isNormal)
				{
					// sparse table: strings are inline
					const unsigned char* ptr = recordOffset;
					for (int f = 0; f <= field->pos; f++)
					{
						if (structure->fields[f]->isKey)
							continue;
						if (structure->fields[f]->type == "uint64")
						{
							ptr += 8;
						}
						else
						{
							std::string v(reinterpret_cast<const char*>(ptr));
							ptr = ptr + v.size() + 1;
						}
					}
					stringPtr = reinterpret_cast<const char*>(ptr);
				}
				else if (m_wdcVersion > 2)
				{
					// WDC3+: string table reference
					const uint32_t dataPos = m_fieldStorageInfo[field->pos].field_offset_bits / 8;

					// compute cumulative record data size of earlier sections
					uint32_t outsideDataSize = 0;
					for (uint32_t s = 0; s < loc.sectionIndex; s++)
						outsideDataSize += m_sections[s].recordDataSize;

					const int32_t absoluteRecordOfs =
						static_cast<int32_t>(loc.localIndex * m_header.record_size) -
						static_cast<int32_t>(m_header.record_count * m_header.record_size);

					const int32_t stringTableIndex =
						static_cast<int32_t>(outsideDataSize) + absoluteRecordOfs +
						static_cast<int32_t>(dataPos) + static_cast<int32_t>(val);

					// find which section's string table contains this index
					const unsigned char* strBase = nullptr;
					for (uint32_t s = 0; s < m_sections.size(); s++)
					{
						const SectionData& strSec = m_sections[s];
						const int32_t localOfs = stringTableIndex - static_cast<int32_t>(strSec.previousStringTableSize);
						if (localOfs >= 0 && static_cast<uint32_t>(localOfs) < strSec.stringTableSize)
						{
							strBase = m_fileData + strSec.stringTableOffset + localOfs;
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
						recordOffset + m_fieldStorageInfo[field->pos].field_offset_bits / 8 + val -
						((m_header.record_count - sec.recordCount) * m_header.record_size));
				}

				std::string value(stringPtr ? stringPtr : "");
				std::replace(value.begin(), value.end(), '"', '\'');
				result.push_back(value);
			}
			else if (field->type == "float")
			{
				std::stringstream ss;
				float fval;
				std::memcpy(&fval, &val, sizeof(float));
				ss << fval;
				result.push_back(ss.str());
			}
			else if (field->type == "int8")
			{
				std::stringstream ss;
				int8_t v;
				std::memcpy(&v, &val, sizeof(int8_t));
				ss << static_cast<int>(v);
				result.push_back(ss.str());
			}
			else if (field->type == "uint8")
			{
				std::stringstream ss;
				uint8_t v;
				std::memcpy(&v, &val, sizeof(uint8_t));
				ss << static_cast<unsigned>(v);
				result.push_back(ss.str());
			}
			else if (field->type == "int16")
			{
				std::stringstream ss;
				int16_t v = static_cast<int16_t>(val & 0xFFFF);
				ss << static_cast<int>(v);
				result.push_back(ss.str());
			}
			else if (field->type == "uint16")
			{
				std::stringstream ss;
				uint16_t v = static_cast<uint16_t>(val & 0xFFFF);
				ss << static_cast<unsigned>(v);
				result.push_back(ss.str());
			}
			else if (field->type == "int32")
			{
				std::stringstream ss;
				int32_t v;
				std::memcpy(&v, &val, sizeof(int32_t));
				ss << v;
				result.push_back(ss.str());
			}
			else if (field->type == "uint32")
			{
				std::stringstream ss;
				uint32_t v;
				std::memcpy(&v, &val, sizeof(uint32_t));
				ss << v;
				result.push_back(ss.str());
			}
			else if (field->type == "int64")
			{
				std::stringstream ss;
				ss << static_cast<int64_t>(val);
				result.push_back(ss.str());
			}
			else if (field->type == "uint64")
			{
				std::stringstream ss;
				ss << static_cast<uint64_t>(val);
				result.push_back(ss.str());
			}
			else if (field->type == "int")
			{
				std::stringstream ss;
				int32_t v;
				std::memcpy(&v, &val, sizeof(int32_t));
				ss << v;
				result.push_back(ss.str());
			}
			else if (field->type == "byte")
			{
				std::stringstream ss;
				uint8_t v = static_cast<uint8_t>(val & 0xFF);
				ss << static_cast<unsigned>(v);
				result.push_back(ss.str());
			}
			else
			{
				std::stringstream ss;
				uint32_t v;
				std::memcpy(&v, &val, sizeof(uint32_t));
				ss << v;
				result.push_back(ss.str());
			}
		}
	}

	return result;
}
