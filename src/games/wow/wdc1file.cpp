#include "wdc1file.h"

#include "logger/Logger.h"

#include "Game.h" // GAMEDIRECTORY Singleton

#include <sstream>

#include <bitset>

#include "WoWDatabase.h"

#define WDC1_READ_DEBUG 0

WDC1File::WDC1File(const QString & file):
WDB5File(file)
{
}

void WDC1File::readWDBC1Header()
{
  read(&m_header, sizeof(WDC1File::header)); // File Header

#if WDC1_READ_DEBUG > 0
  LOG_INFO << "magic" << m_header.magic[0] << m_header.magic[1] << m_header.magic[2] << m_header.magic[3];
  LOG_INFO << "record count" << m_header.record_count;
  LOG_INFO << "field count" << m_header.field_count;
  LOG_INFO << "record size" << m_header.record_size;
  LOG_INFO << "string table size" << m_header.string_table_size;
  LOG_INFO << "layout hash" << m_header.layout_hash;
  LOG_INFO << "min id" << m_header.min_id;
  LOG_INFO << "max id" << m_header.max_id;
  LOG_INFO << "locale" << m_header.locale;
  LOG_INFO << "copy table size" << m_header.copy_table_size;
  LOG_INFO << "flags" << m_header.flags;
  LOG_INFO << "id index" << m_header.id_index;
  LOG_INFO << "total_field_count" << m_header.total_field_count;
  LOG_INFO << "bitpacked_data_offset" << m_header.bitpacked_data_offset;
  LOG_INFO << "lookup_column_count" << m_header.lookup_column_count;
  LOG_INFO << "offset_map_offset" << m_header.offset_map_offset;
  LOG_INFO << "id_list_size" << m_header.id_list_size;
  LOG_INFO << "field_storage_info_size" << m_header.field_storage_info_size;
  LOG_INFO << "common_data_size" << m_header.common_data_size;
  LOG_INFO << "pallet_data_size" << m_header.pallet_data_size;
  LOG_INFO << "relationship_data_size" << m_header.relationship_data_size;
#endif
}

bool WDC1File::open()
{
  if (!CASCFile::open())
  {
    LOG_ERROR << "An error occured while trying to read the DBCFile" << fullname();
    return false;
  }

  readWDBC1Header();

  recordSize = m_header.record_size;
  recordCount = m_header.record_count;
  fieldCount = m_header.field_count;

  field_structure * fieldStructure = new field_structure[fieldCount];
  read(fieldStructure, fieldCount * sizeof(field_structure));
#if WDB5_READ_DEBUG > 0
  LOG_INFO << "--------------------------";
#endif
  for (uint i = 0; i < fieldCount; i++)
  {
#if WDC1_READ_DEBUG > 0
    LOG_INFO << "pos" << fieldStructure[i].position << "size :" << fieldStructure[i].size << "->" << (32 - fieldStructure[i].size) / 8 << "bytes";
#endif
    m_fieldSizes[fieldStructure[i].position] = fieldStructure[i].size;
  }
#if WDC1_READ_DEBUG > 0
  LOG_INFO << "--------------------------";
#endif

  stringSize = m_header.string_table_size;

  data = getPointer();
  stringTable = data + recordSize*recordCount;

  // compute various offset needed to read data in the file 
  uint32 stringTableOffset = sizeof(header) + 4 * fieldCount + recordSize * recordCount;

  // embedded strings in fields instead of stringTable
  if ((m_header.flags & 0x01) != 0)
  {
    stringSize = 0;
    stringTableOffset = m_header.offset_map_offset + 6 * (m_header.max_id - m_header.min_id + 1);
  }
  
  uint32 IdBlockOffset = stringTableOffset + stringSize;

  uint32 copyBlockOffset = IdBlockOffset;

  if ((m_header.flags & 0x04) != 0)
    copyBlockOffset += (recordCount * 4);

  uint32 fieldStorageInfoOffset = copyBlockOffset + m_header.copy_table_size;
  uint32 palletDataOffset = fieldStorageInfoOffset + m_header.field_storage_info_size;
  uint32 commonBlockOffset = palletDataOffset + m_header.pallet_data_size;
  uint32 relationshipDataOffset = commonBlockOffset + m_header.common_data_size;
 
#if WDC1_READ_DEBUG > 0
  LOG_INFO << "m_header.flags & 0x01" << (m_header.flags & 0x01);
  LOG_INFO << "m_header.flags & 0x04" << (m_header.flags & 0x04);
  LOG_INFO << "stringTableOffset" << stringTableOffset;
  LOG_INFO << "IdBlockOffset" << IdBlockOffset;
  LOG_INFO << "copyBlockOffset" << copyBlockOffset;
  LOG_INFO << "fieldStorageInfoOffset" << fieldStorageInfoOffset;
  LOG_INFO << "palletDataOffset" << palletDataOffset;
  LOG_INFO << "commonBlockOffset" << commonBlockOffset;
  LOG_INFO << "relationshipDataOffset" << relationshipDataOffset;
#endif
  /*
  LOG_INFO << fullname();
  LOG_INFO << "copy table size" << m_header.copy_table_size;
  LOG_INFO << "common_data_size" << m_header.common_data_size;
  LOG_INFO << "pallet_data_size" << m_header.pallet_data_size;
  LOG_INFO << "relationship_data_size" << m_header.relationship_data_size;
    */
  // read storage info
  if (m_header.field_storage_info_size > 0)
  {
    seek(fieldStorageInfoOffset);

    for (uint i = 0; i < (m_header.field_storage_info_size / sizeof(field_storage_info)); i++)
    {
      field_storage_info info;
      read(&info, sizeof(info));
      m_fieldStorageInfo.push_back(info);
    }
#if WDC1_READ_DEBUG > 0
    LOG_INFO << fullname() << "----- BEGIN -------";
    for (auto it : m_fieldStorageInfo)
    {
      LOG_INFO << "------";
      LOG_INFO << "storage type :" << it.storage_type;
      LOG_INFO << "field offset bits :" << it.field_offset_bits;
      LOG_INFO << "field size bits :" << it.field_size_bits;
      LOG_INFO << "additional data size :" << it.additional_data_size;
    }
    LOG_INFO << fullname() << "----- END -------";
#endif
  }

  // sparse Table
  if ((m_header.flags & 0x01) != 0)
  {
    m_isSparseTable = true;
    seek(stringSize);

    recordCount = 0;

    for (uint i = 0; i < (m_header.max_id - m_header.min_id + 1); i++)
    {
      uint32 offset;
      uint16 length;

      read(&offset, sizeof(offset));
      read(&length, sizeof(length));

      if ((offset == 0) || (length == 0))
        continue;

      m_IDs.push_back(m_header.min_id + i);
      m_recordOffsets.push_back(buffer + offset);
      recordCount++;
    }
  }
  else
  {
    m_IDs.reserve(recordCount);
    m_recordOffsets.reserve(recordCount);

    // read IDs
    if ((m_header.flags & 0x04) != 0)
    {
      seek(IdBlockOffset);
#if WDC1_READ_DEBUG > 0
      LOG_INFO << "(header.flags & 0x04) != 0 -- BEGIN";
#endif
      uint32 * vals = new uint32[recordCount];
      read(vals, recordCount * sizeof(uint32));
      m_IDs.assign(vals, vals + recordCount);
      delete[] vals;
#if WDC1_READ_DEBUG > 0
      LOG_INFO << "(header.flags & 0x04) != 0 -- END";
#endif
    }
    else
    {
      field_storage_info info = m_fieldStorageInfo[m_header.id_index];

#if WDC1_READ_DEBUG > 0     
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
        unsigned char * recordOffset = data + (i*recordSize);
        switch (info.storage_type)
        {
          case FIELD_COMPRESSION::NONE: // same as wdb5
          {
            unsigned char * val = new unsigned char[info.field_size_bits/8];
            memcpy(val, recordOffset, info.field_size_bits / 8);
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
            // id = id >> (info.field_offset_bits & 7);
            id = id & ((1ull << info.field_size_bits) - 1);
            m_IDs.push_back(id);

            break;
          }
          case FIELD_COMPRESSION::COMMON_DATA:
            break;
          case FIELD_COMPRESSION::BITPACKED_INDEXED:
            break;
          case FIELD_COMPRESSION::BITPACKED_INDEXED_ARRAY:
            break;
        }
      }
    }

      // store offsets
      for (uint i = 0; i < recordCount; i++)
        m_recordOffsets.push_back(data + (i*recordSize));
    }
    return true;
  }


bool WDC1File::close()
{
  return WDB5File::close();
}

std::vector<std::string> WDC1File::get(unsigned int recordIndex, const core::TableStructure * structure) const
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

    for (uint i = 0; i < field->arraySize; i++)
    {
      result.push_back("");
      continue;

      int fieldSize = (32 - m_fieldSizes.at(field->pos)) / 8;
      unsigned char * val = new unsigned char[fieldSize];

      memcpy(val, recordOffset + field->pos + i*fieldSize, fieldSize);

      if (field->type == "text")
      {
        char * stringPtr;
        if (m_isSparseTable)
          stringPtr = reinterpret_cast<char *>(recordOffset + field->pos);
        else
          stringPtr = reinterpret_cast<char *>(stringTable + *reinterpret_cast<int *>(val));

        std::string value(stringPtr);
        std::replace(value.begin(), value.end(), '"', '\'');
        result.push_back(value);
      }
      else if (field->type == "float")
      {
        std::stringstream ss;
        ss << *reinterpret_cast<float*>(val);
        result.push_back(ss.str());
      }
      else if (field->type == "int")
      {
        std::stringstream ss;
        ss << (*reinterpret_cast<int*>(val));
        result.push_back(ss.str());
      }
      else
      {
        unsigned int mask = 0xFFFFFFFF;
        if (fieldSize == 1)
          mask = 0x000000FF;
        else if (fieldSize == 2)
          mask = 0x0000FFFF;
        else if (fieldSize == 3)
          mask = 0x00FFFFFF;

        std::stringstream ss;
        ss << (*reinterpret_cast<unsigned int*>(val)& mask);
        result.push_back(ss.str());
      }
    }
  }

  return result;
}

WDC1File::~WDC1File()
{
  close();
}
