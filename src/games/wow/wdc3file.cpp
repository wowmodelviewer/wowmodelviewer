#include "wdc3file.h"

#include "logger/Logger.h"

#include "Game.h" // GAMEDIRECTORY Singleton

#include <sstream>

#include <bitset>

#include "WoWDatabase.h"

#define WDC3_READ_DEBUG 3
#define WDC3_READ_DEBUG_FIRST_RECORDS 0

WDC3File::WDC3File(const QString & file):
WDB5File(file)
{
}

void WDC3File::readWDC3Header()
{
  read(&m_header, sizeof(WDC3File::header)); // File Header

#if WDC3_READ_DEBUG > 1
  LOG_INFO << "magic" << m_header.magic[0] << m_header.magic[1] << m_header.magic[2] << m_header.magic[3];
  LOG_INFO << "record count" << m_header.record_count;
  LOG_INFO << "field count" << m_header.field_count;
  LOG_INFO << "record size" << m_header.record_size;
  LOG_INFO << "string table size" << m_header.string_table_size;
  LOG_INFO << "layout hash" << m_header.layout_hash;
  LOG_INFO << "min id" << m_header.min_id;
  LOG_INFO << "max id" << m_header.max_id;
  LOG_INFO << "locale" << m_header.locale;
  LOG_INFO << "flags" << m_header.flags;
  LOG_INFO << "id index" << m_header.id_index;
  LOG_INFO << "total_field_count" << m_header.total_field_count;
  LOG_INFO << "bitpacked_data_offset" << m_header.bitpacked_data_offset;
  LOG_INFO << "lookup_column_count" << m_header.lookup_column_count;
  LOG_INFO << "field_storage_info_size" << m_header.field_storage_info_size;
  LOG_INFO << "common_data_size" << m_header.common_data_size;
  LOG_INFO << "pallet_data_size" << m_header.pallet_data_size;
  LOG_INFO << "section_count" << m_header.section_count;
#endif
}

bool WDC3File::open()
{
  if (!CASCFile::open(false))
  {
    LOG_ERROR << "An error occured while trying to read the DBCFile" << fullname();
    return false;
  }

  readWDC3Header();

  recordSize = m_header.record_size;
  recordCount = m_header.record_count;
  fieldCount = m_header.field_count;
  stringSize = m_header.string_table_size;

  section_header * sectionHeader = new section_header[m_header.section_count];
  read(sectionHeader, sizeof(section_header) * m_header.section_count);

#if WDC3_READ_DEBUG > 1
  for (uint i = 0; i < m_header.section_count; i++)
  {
    LOG_INFO << "---------------SECTION--------------------";
    LOG_INFO << "tact_key_hash" << sectionHeader[i].tact_key_hash;
    LOG_INFO << "file_offset" << sectionHeader[i].file_offset;
    LOG_INFO << "record_count" << sectionHeader[i].record_count;
    LOG_INFO << "string_table_size" << sectionHeader[i].string_table_size;
    LOG_INFO << "offset_records_end" << sectionHeader[i].offset_records_end;
    LOG_INFO << "id_list_size" << sectionHeader[i].id_list_size;
    LOG_INFO << "relationship_data_size" << sectionHeader[i].relationship_data_size;
    LOG_INFO << "offset_map_id_count" << sectionHeader[i].offset_map_id_count;
    LOG_INFO << "copy_table_count" << sectionHeader[i].copy_table_count;
    LOG_INFO << "------------------------------------------";
  }
#endif

  field_structure * fieldStructure = new field_structure[fieldCount];
  read(fieldStructure, fieldCount * sizeof(field_structure));
#if WDC3_READ_DEBUG > 3
  LOG_INFO << "--------------------------";
#endif
  for (uint i = 0; i < fieldCount; i++)
  {
#if WDC3_READ_DEBUG > 3
    LOG_INFO << "pos" << fieldStructure[i].position << "size :" << fieldStructure[i].size << "->" << (32 - fieldStructure[i].size) / 8 << "bytes";
#endif
    m_fieldSizes[fieldStructure[i].position] = fieldStructure[i].size;
  }
#if WDC3_READ_DEBUG > 3
  LOG_INFO << "--------------------------";
#endif


  // read storage info
  if (m_header.field_storage_info_size > 0)
  {
    for (uint i = 0; i < (m_header.field_storage_info_size / sizeof(field_storage_info)); i++)
    {
      field_storage_info info;
      read(&info, sizeof(info));
      m_fieldStorageInfo.push_back(info);
    }
#if WDC3_READ_DEBUG > 3
    LOG_INFO << fullname() << "----- BEGIN -------";
    uint fieldId = 0;
    for (auto it : m_fieldStorageInfo)
    {
      LOG_INFO << "--" << fieldId++ << "--";
      LOG_INFO << "storage type :" << it.storage_type;
      LOG_INFO << "field offset bits :" << it.field_offset_bits;
      LOG_INFO << "field size bits :" << it.field_size_bits;
      LOG_INFO << "additional data size :" << it.additional_data_size;
      LOG_INFO << "val1 :" << it.val1;
      LOG_INFO << "val2 :" << it.val2;
      LOG_INFO << "val3 :" << it.val3;
    }
    LOG_INFO << fullname() << "----- END -------";
#endif
  }

  // read pallet data
  if (m_header.pallet_data_size > 0)
  {
    m_palletData = new unsigned char[m_header.pallet_data_size];
    read(m_palletData, m_header.pallet_data_size);

    uint fieldId = 0;
    uint32 offset = 0;
    for (auto it : m_fieldStorageInfo)
    {
      if ((it.storage_type == FIELD_COMPRESSION::BITPACKED_INDEXED ||
        it.storage_type == FIELD_COMPRESSION::BITPACKED_INDEXED_ARRAY) &&
        (it.additional_data_size != 0))
      {
        m_palletBlockOffsets[fieldId] = offset;
#if WDC3_READ_DEBUG > 4
        LOG_INFO << fieldId << "=>" << palletBlockOffset;
#endif
        offset += it.additional_data_size;
      }
      fieldId++;
    }
  }

  // read common data
  unsigned char * commonData = 0;
  if (m_header.common_data_size > 0)
  {
    commonData = new unsigned char[m_header.common_data_size];
    read(commonData, m_header.common_data_size);

    uint fieldId = 0;
    size_t offset = 0;
    for (auto it : m_fieldStorageInfo)
    {
      if ((it.storage_type == FIELD_COMPRESSION::COMMON_DATA) && (it.additional_data_size != 0))
      {
        unsigned char * ptr = commonData + offset;

        std::map<uint32, uint32> commonVals;
#if WDC3_READ_DEBUG > 4
        LOG_INFO << "Field" << fieldId;
#endif
        for (uint i = 0; i < it.additional_data_size / 8; i++)
        {
          uint32 id;
          uint32 val;                                                                                                                                 
          memcpy(&id, ptr, 4);
          ptr += 4;
          memcpy(&val, ptr, 4);
          ptr += 4;

#if WDC3_READ_DEBUG > 4
          LOG_INFO << id << "=>" << val;
#endif
          commonVals[id] = val;
        }
        m_commonData[fieldId] = commonVals;
        offset += it.additional_data_size;

      }
      fieldId++;
    }
    delete[] commonData;
  }

  // a section = 
  // 1. records
  // 2. string block
  // 3. id list
  // 4. copy table
  // 5. offset map 
  // 6. offset map id list
  // 7. relationship map 

  // this only supports one section. We need to be able to loop and support multiple sections.
  // compute section size is, for now, either :
  // - nb sections is 2, then it's section 2 beginning - section 1 beginning
  // - nb sections is 1, then it's filesize - section 1 beginning
  size_t sectionSize = 0;

  if (m_header.section_count == 1)
    sectionSize = size - sectionHeader[0].file_offset;
  else
    sectionSize = sectionHeader[1].file_offset - sectionHeader[0].file_offset;                                                      

  m_sectionData = new unsigned char[sectionSize];
  read(m_sectionData, sectionSize);

  // maintain curPtr to location of the block along section reading
  unsigned char * curPtr = m_sectionData;

  // 1. records
  // note that we are only storing offset to the different record, actual reading is performed in get method
  if ((m_header.flags & 0x01) == 0) // non sparse table
  { 
    m_recordOffsets.reserve(recordCount);
    // store offsets
    for (uint i = 0; i < recordCount; i++)
      m_recordOffsets.push_back(m_sectionData + (i*recordSize));

    curPtr += (recordSize * recordCount);
  }
  else
  {
    curPtr += sectionHeader[0].offset_records_end;
  }

  LOG_INFO << "nb records" << m_recordOffsets.size();
  // 2. string block
  // only update curPtr, actual values are read in get method when accessing record
#if WDC3_READ_DEBUG > 4
  LOG_INFO << "---- STRING TABLE ----";
  for (uint i = 0; i < sectionHeader[0].string_table_size; i += 5)
  {
    std::string value(reinterpret_cast<char *>(curPtr+i));
    LOG_INFO << i << value.c_str();
  }
  LOG_INFO << "---- STRING TABLE ----";
#endif
  curPtr += sectionHeader[0].string_table_size;

  // 3. id list
  if (sectionHeader[0].id_list_size > 0)
  {
    size_t nbId = sectionHeader[0].id_list_size / 4;
    m_IDs.reserve(nbId);

    uint32 * vals = new uint32[nbId];
    memcpy(vals, curPtr, nbId * sizeof(uint32));
    curPtr += sectionHeader[0].id_list_size;

    m_IDs.assign(vals, vals + nbId);
    delete[] vals;
  }
  else
  {
    m_IDs.reserve(recordCount);
    
    field_storage_info info = m_fieldStorageInfo[m_header.id_index];

#if WDC3_READ_DEBUG > 2     
    LOG_INFO << "info.storage_type" << info.storage_type;
    LOG_INFO << "info.field_size_bits" << info.field_size_bits;
    LOG_INFO << "info.field_offset_bits" << info.field_offset_bits;

    if (info.storage_type)
    {
      LOG_INFO << "size" << (info.field_size_bits + (info.field_offset_bits & 7) + 7) / 8;
      LOG_INFO << "offset" << info.field_offset_bits / 8;
    }
#endif
    // read ids from data
    for (uint i = 0; i < recordCount; i++)
    {
      unsigned char * recordOffset = m_sectionData + (i*recordSize);
      switch (info.storage_type)
      {
        case FIELD_COMPRESSION::NONE:
        {
          unsigned char * val = new unsigned char[info.field_size_bits / 8];
          memcpy(val, recordOffset + info.field_offset_bits / 8, info.field_size_bits / 8);
          m_IDs.push_back((*reinterpret_cast<unsigned int*>(val)));
          break;
        }
        case FIELD_COMPRESSION::BITPACKED:
        {
          unsigned int size = (info.field_size_bits + (info.field_offset_bits & 7) + 7) / 8;
          unsigned int offset = info.field_offset_bits / 8;
          unsigned char * val = new unsigned char[size];

          memcpy(val, recordOffset + offset, size);

          unsigned int id = (*reinterpret_cast<unsigned int*>(val));
          id = id & ((1ull << info.field_size_bits) - 1);

          m_IDs.push_back(id);
          break;
        }
        case FIELD_COMPRESSION::COMMON_DATA:
          LOG_ERROR << "Reading ID from Common Data is not implemented";
          return false;
        case FIELD_COMPRESSION::BITPACKED_INDEXED:
        case FIELD_COMPRESSION::BITPACKED_SIGNED:
        {
          uint id = readBitpackedValue(info, recordOffset);
          m_IDs.push_back(id);
          break;
        }
        case FIELD_COMPRESSION::BITPACKED_INDEXED_ARRAY:
          LOG_ERROR << "Reading ID from Bitpacked Indexed Array is not implemented";
          return false;
        default:
          LOG_ERROR << "Reading ID from type" << info.storage_type << "is not implemented";
          return false;
      }
    }
  }

  // 4. copy table
  if (sectionHeader[0].copy_table_count > 0)
  {
    uint nbEntries = sectionHeader[0].copy_table_count;

    m_IDs.reserve(recordCount + nbEntries);
    m_recordOffsets.reserve(recordCount + nbEntries);

    copy_table_entry * copyTable = new copy_table_entry[nbEntries];
    memcpy(copyTable, curPtr, sectionHeader[0].copy_table_count * 8);
    curPtr += (sectionHeader[0].copy_table_count *8);

    // create a id->offset map
    std::map<uint32, unsigned char*> IDToOffsetMap;

    for (uint i = 0; i < recordCount; i++)
    {
      IDToOffsetMap[m_IDs[i]] = m_recordOffsets[i];
    }

    for (uint i = 0; i < nbEntries; i++)
    {
      copy_table_entry entry = copyTable[i];
      m_IDs.push_back(entry.newRowId);
      m_recordOffsets.push_back(IDToOffsetMap[entry.copiedRowId]);
    }

    delete[] copyTable;

    recordCount += nbEntries;
  }

  // 5. offset map
  if (sectionHeader[0].offset_map_id_count > 0) // sparse table
  {
    m_isSparseTable = true;

    for (uint i = 0; i < sectionHeader[0].offset_map_id_count; i++)
    {
      uint32 offset;
      uint16 length;

      memcpy(&offset, curPtr, sizeof(offset));
      curPtr += sizeof(offset);
      memcpy(&length, curPtr, sizeof(length));
      curPtr += sizeof(length);

      if ((offset == 0) || (length == 0))
        continue;

      m_recordOffsets.push_back(m_sectionData + offset);
      recordCount++;
    }
  }

  // 6. offset map id list
  // ?

  // 7. relationship map
  if (sectionHeader[0].relationship_data_size > 0)
  {
    struct relationship_entry
    {
      uint32 foreign_id;
      uint32 record_index;
    };

    uint32 nbEntries;
    memcpy(&nbEntries, curPtr, 4);
    curPtr += (4 + 8);
    LOG_INFO << "nbEntries" << nbEntries;

    for (uint i = 0; i < nbEntries; i++)
    {
      uint32 foreignKey;
      uint32 recordIndex;
      memcpy(&foreignKey, curPtr, 4);
      curPtr += 4;
      memcpy(&recordIndex, curPtr, 4);
      curPtr += 4;
      std::stringstream ss;
      ss << foreignKey;
      m_relationShipData[recordIndex] = ss.str();
    }

#if WDC3_READ_DEBUG > 4
    LOG_INFO << "---- RELATIONSHIP DATA ----";
    for (auto it : m_relationShipData)
      LOG_INFO << it.first << "->" << it.second.c_str();
    LOG_INFO << "---- RELATIONSHIP DATA ----";
#endif
  }

  LOG_INFO << "nb ids" << m_IDs.size();
  LOG_INFO << "nb records" << m_recordOffsets.size();
  LOG_INFO << "nb relationship data" << m_relationShipData.size();

  /*
   // For reverse engineering purpose only
  for (uint r = 69; r < 75; r++)
  {
    unsigned char * offset = m_recordOffsets[r];
    unsigned char * nextoff = m_recordOffsets[r+1];
    for (uint i = 0; i < (nextoff - offset); i++)
    {
      LOG_INFO << i << *(offset + i) << *(reinterpret_cast<char *>(offset + i));

    }
  }
  */




#if WDC3_READ_DEBUG_FIRST_RECORDS > 0
  for (uint id = 0; id < 10; id++)
  {
    uint plop = 0;
    LOG_INFO << "VALUES FOR ID" << m_IDs[id] << "---- BEGIN ----";
    for (auto it : m_fieldStorageInfo)
    {
      unsigned int val = 0;
      if (it.field_size_bits == 96)
      {
        for (uint i = 0; i < 3; i++)
        {
          if (readFieldValue(id, plop, i, 3, val))
          {
            std::string value = "ERROR";
            if (val < stringSize)
              value = reinterpret_cast<char *>(stringTable + val);
            LOG_INFO << plop << "-" << i << "(" << it.storage_type << ")" << val << value.c_str();
          }
        }
        plop++;
        continue;
      }
      if ((it.storage_type == FIELD_COMPRESSION::BITPACKED_INDEXED_ARRAY) &&
          (it.val3 != 1))
      {
        for (uint i = 0; i < it.val3; i++)
        {
          if (readFieldValue(id, plop, i, 3, val))
          {
            std::string value = "ERROR";
            if (val < stringSize)
              value = reinterpret_cast<char *>(stringTable + val);
            LOG_INFO << plop << "-" << i << "(" << it.storage_type << ")" << val << value.c_str();
          }
        }
        plop++;
        continue;
      }
      if (readFieldValue(id, plop, 0, 1, val))
      {
        std::string value = "ERROR";
        if (val < stringSize)
          value = reinterpret_cast<char *>(stringTable + val);
        LOG_INFO << plop << "(" << it.storage_type << ")" << val << value.c_str();
      }

      plop++;
    }
    LOG_INFO << "VALUES FOR ID" << m_IDs[id] << "---- END ----";
  }
#endif
  return true;
}


bool WDC3File::close()
{
  return WDB5File::close();
}

std::vector<std::string> WDC3File::get(unsigned int recordIndex, const core::TableStructure * structure) const
{
  std::vector<std::string> result;
  unsigned char * recordOffset = m_recordOffsets[recordIndex];

  for (auto it : structure->fields)
  {
    wow::FieldStructure * field = dynamic_cast<wow::FieldStructure *>(it);

    if (field->isKey)
    {
      std::stringstream ss;
      ss << m_IDs[recordIndex];
      result.push_back(ss.str());
      continue;
    }

    if (field->isRelationshipData)
    {
      std::stringstream ss;
      auto it = m_relationShipData.find(recordIndex);
      if (it != m_relationShipData.end())
        result.push_back(it->second);
      else
        result.push_back("");
      continue;
    }

    for (uint i = 0; i < field->arraySize; i++)
    {
      unsigned int val = 0;
      if (!readFieldValue(recordIndex, field->pos, i, field->arraySize, val))
        continue;

      if (field->type == "text")
      {
        char * stringPtr;
        if (m_isSparseTable)
        {
          unsigned char * ptr = recordOffset;
          // iterate along record to get right position
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
              std::string val(reinterpret_cast<char *>(ptr));
              ptr = ptr + val.size() + 1;
            }
          }
          stringPtr = reinterpret_cast<char *>(ptr);
        }
        else
        {
          stringPtr = reinterpret_cast<char *>(recordOffset + m_fieldStorageInfo[field->pos].field_offset_bits / 8 + val);
        }

        std::string value(stringPtr);
        std::replace(value.begin(), value.end(), '"', '\'');
        result.push_back(value);
      }
      else if (field->type == "float")
      {
        std::stringstream ss;
        ss << *reinterpret_cast<float *>(&val);
        result.push_back(ss.str());
      }
      else if (field->type == "int")
      {
        std::stringstream ss;
        ss << *reinterpret_cast<int32 *>(&val);
        result.push_back(ss.str());
      }
      else if (field->type == "uint16")
      {
        std::stringstream ss;
        ss << *reinterpret_cast<uint16 *>(&val);
        result.push_back(ss.str());
      }
      else if (field->type == "byte")
      {
        std::stringstream ss;
        ss << (*reinterpret_cast<uint16 *>(&val) & 0x000000FF);
        result.push_back(ss.str());
      }
      else if (field->type == "uint64")
      {
        std::stringstream ss;
        ss << *reinterpret_cast<long *>(&val);
        result.push_back(ss.str());
      }
      else
      {
        std::stringstream ss;
        ss << *reinterpret_cast<uint32 *>(&val);
        result.push_back(ss.str());
      }
    }
  }

  return result;
}

WDC3File::~WDC3File()
{
  close();
  delete [] m_sectionData;
  delete [] m_palletData;
}

bool WDC3File::readFieldValue(unsigned int recordIndex, unsigned int fieldIndex, uint arrayIndex, uint arraySize, unsigned int & result) const
{
  unsigned char * recordOffset = m_recordOffsets[recordIndex];
  field_storage_info info = m_fieldStorageInfo[fieldIndex];

  switch (info.storage_type)
  {
    case FIELD_COMPRESSION::NONE:
    {
      uint fieldSize = info.field_size_bits / 8;
      unsigned char * fieldOffset = recordOffset + info.field_offset_bits / 8;
  
      if (arraySize != 1)
      {
        fieldSize /= arraySize;
        fieldOffset += ((info.field_size_bits / 8 / arraySize) * arrayIndex);
      }

      unsigned char * val = new unsigned char[fieldSize];
      memcpy(val, fieldOffset, fieldSize);
      
      // handle special case => when value is supposed to be 0, values read are all 0xFF
      // Don't understand why, so I use this ugly stuff...
      if (arraySize != 1)
      {
        uint nbFF = 0;
        for (uint i = 0; i < fieldSize; i++)
        {
          if (val[i] == 0xFF)
            nbFF++;
        }

        if (nbFF == fieldSize)
        {
          for (uint i = 0; i < fieldSize; i++)
            val[i] = 0;
        }
      }
      result = (*reinterpret_cast<unsigned int*>(val));
      result = result & ((1ull << (info.field_size_bits / arraySize)) - 1);
      break;
    }
    case FIELD_COMPRESSION::BITPACKED:
    case FIELD_COMPRESSION::BITPACKED_SIGNED:
    {
      result = readBitpackedValue(info, recordOffset);
      break;
    }
    case FIELD_COMPRESSION::COMMON_DATA:
    { 
      result = info.val1;
      auto mapIt = m_commonData.find(fieldIndex);
      if (mapIt != m_commonData.end())
      {
        auto valIt = mapIt->second.find(m_IDs[recordIndex]);
        if (valIt != mapIt->second.end())
          result = valIt->second;
      }
      break;
    }
    case FIELD_COMPRESSION::BITPACKED_INDEXED:
    {                                          
      uint32 index = readBitpackedValue(info, recordOffset);
      auto it = m_palletBlockOffsets.find(fieldIndex);
      uint32 offset = it->second + index * 4;
      memcpy(&result, m_palletData + offset, 4);
      break;
    }
    case FIELD_COMPRESSION::BITPACKED_INDEXED_ARRAY:
    {
      uint32 index = readBitpackedValue(info, recordOffset);
      auto it = m_palletBlockOffsets.find(fieldIndex);
      uint32 offset = it->second + index * arraySize * 4 + arrayIndex * 4;
      memcpy(&result, m_palletData + offset, 4);
      break;
    }

    default:
      LOG_ERROR << "Reading data from type" << info.storage_type << "is not implemented";
      return false;
  }
  return true;
}

uint32 WDC3File::readBitpackedValue(field_storage_info info, unsigned char * recordOffset) const
{
  unsigned int size = (info.field_size_bits + (info.field_offset_bits & 7) + 7) / 8;
  unsigned int offset = info.field_offset_bits / 8;
  unsigned char * v = new unsigned char[size];

  memcpy(v, recordOffset + offset, size);

  uint32 result = (*reinterpret_cast<unsigned int*>(v));
  result = result >> (info.field_offset_bits & 7);
  result = result & ((1ull << info.field_size_bits) - 1);
  return result;
}




