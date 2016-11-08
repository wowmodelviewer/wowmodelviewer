#include "wdb5file.h"

#include "logger/Logger.h"

#include <sstream>

#include <bitset>

#define WDB5_READ_DEBUG 0

WDB5File::WDB5File(const QString & file) :
DBFile(file), m_isSparseTable(false), m_IDs(0)
{
}

struct wdb5_db2_header
{
  char magic[4];                                               // 'WDB5' for .db2 (database)
  uint32 record_count;
  uint32 field_count;                                         // for the first time, this counts arrays as '1'; in the past, only the WCH* variants have counted arrays as 1 field
  uint32 record_size;
  uint32 string_table_size;                                   // if flags & 0x01 != 0, this field takes on a new meaning - it becomes an absolute offset to the beginning of the offset_map
  uint32 table_hash;
  uint32 layout_hash;                                         // used to be 'build', but after build 21737, this is a new hash field that changes only when the structure of the data changes
  uint32 min_id;
  uint32 max_id;
  uint32 locale;                                              // as seen in TextWowEnum
  uint32 copy_table_size;
  uint16 flags;                                               // in WDB3/WCH4, this field was in the WoW executable's DBCMeta; possible values are listed in Known Flag Meanings
  uint16 id_index;                                            // new in WDB5 (and only after build 21737), this is the index of the field containing ID values; this is ignored if flags & 0x04 != 0
};


bool WDB5File::doSpecializedOpen()
{
  wdb5_db2_header header;
  read(&header, sizeof(wdb5_db2_header)); // File Header

#if WDB5_READ_DEBUG > 0
  LOG_INFO << "magic" << header.magic[0] << header.magic[1] << header.magic[2] << header.magic[3];
  LOG_INFO << "record count" << header.record_count;
  LOG_INFO << "field count" << header.field_count;
  LOG_INFO << "record size" << header.record_size;
  LOG_INFO << "string table size" << header.string_table_size;
  LOG_INFO << "layout hash" << header.layout_hash;
  LOG_INFO << "min id" << header.min_id;
  LOG_INFO << "max id" << header.max_id;
  LOG_INFO << "locale" << header.locale;
  LOG_INFO << "copy table size" << header.copy_table_size;
  LOG_INFO << "flags" << header.flags;
  LOG_INFO << "id index" << header.id_index;
#endif

  recordSize = header.record_size;
  recordCount = header.record_count;
  fieldCount = header.field_count;


  field_structure * fieldStructure = new field_structure[fieldCount];
  read(fieldStructure, fieldCount * sizeof(field_structure));
#if WDB5_READ_DEBUG > 0
  LOG_INFO << "--------------------------";
#endif
  for (uint i = 0; i < fieldCount; i++)
  {
#if WDB5_READ_DEBUG > 0
    LOG_INFO << "pos" << fieldStructure[i].position << "size :" << fieldStructure[i].size << "->" << (32 - fieldStructure[i].size) / 8 << "bytes";
#endif
    m_fieldSizes[fieldStructure[i].position] = fieldStructure[i].size;
  }
#if WDB5_READ_DEBUG > 0
  LOG_INFO << "--------------------------";
#endif

  stringSize = header.string_table_size;

  data = getPointer();
  stringTable = data + recordSize*recordCount;

  if ((header.flags & 0x01) != 0)
  {
    m_isSparseTable = true;
    seek(stringSize);

#if WDB5_READ_DEBUG > 0   
    LOG_INFO << "record count - before " << recordCount;
#endif

    recordCount = 0;

    for (uint i = 0; i < (header.max_id - header.min_id + 1); i++)
    {
      uint32 offset;
      uint16 length;

      read(&offset, sizeof(offset));
      read(&length, sizeof(length));

      if ((offset == 0) || (length == 0))
        continue;

	    m_sparseRecords.push_back(std::make_tuple(offset, length, header.min_id+i));

      recordCount++;
    }
#if WDB5_READ_DEBUG > 0
    LOG_INFO << "record count - after" << recordCount;
#endif
  }
  else
  {
    // read IDs
    seekRelative(recordSize*recordCount + stringSize);
    if ((header.flags & 0x04) != 0)
    {
      m_IDs = new uint32[recordCount];
      read(m_IDs, recordCount * sizeof(uint32));
    }
    else
    {
      m_indexPos = fieldStructure[header.id_index].position;
#if WDB5_READ_DEBUG > 0
      LOG_INFO << "m_indexPos" << m_indexPos;
#endif
      m_indexSize = ((32 - fieldStructure[header.id_index].size) / 8);
#if WDB5_READ_DEBUG > 0
      LOG_INFO << "m_indexSize" << m_indexSize;
#endif
    }
  }

  return true;
}

WDB5File::~WDB5File()
{
  close();
}

std::vector<std::string> WDB5File::get(unsigned int recordIndex, const GameDatabase::tableStructure & structure) const
{
  std::vector<std::string> result;

  unsigned char * recordOffset = data + (recordIndex*recordSize);

  if (m_isSparseTable)
  {
    recordOffset = buffer + std::get<0>(m_sparseRecords[recordIndex]);
  }

  for (auto it = structure.fields.begin(), itEnd = structure.fields.end();
       it != itEnd;
       ++it)
  {
    if (it->isKey)
    {
      if (m_IDs != 0)
      {
        std::stringstream ss;
        ss << m_IDs[recordIndex];
        result.push_back(ss.str());
      }
      else if (m_isSparseTable)
      {
        std::stringstream ss;
        ss << std::get<2>(m_sparseRecords[recordIndex]);
        result.push_back(ss.str());
      }
      else
      {
        unsigned char * val = new unsigned char[m_indexSize];
        memcpy(val, recordOffset + m_indexPos, m_indexSize);
        std::stringstream ss;

        unsigned int mask = 0xFFFFFFFF;
        if (m_indexSize == 1)
          mask = 0x000000FF;
        else if (m_indexSize == 2)
          mask = 0x0000FFFF;
        else if (m_indexSize == 3)
          mask = 0x00FFFFFF;

        ss << (*reinterpret_cast<unsigned int*>(val)& mask);
        result.push_back(ss.str());
      }

      continue;
    }

    for (uint i = 0; i < it->arraySize; i++)
    {
      int fieldSize = (32 - m_fieldSizes.at(it->pos)) / 8;
      unsigned char * val = new unsigned char[fieldSize];

      memcpy(val, recordOffset + it->pos + i*fieldSize, fieldSize);

      if (it->type == "text")
      {
        char * stringPtr;
        if (m_isSparseTable)
          stringPtr = reinterpret_cast<char *>(recordOffset + it->pos);
        else
          stringPtr = reinterpret_cast<char *>(stringTable + *reinterpret_cast<int *>(val));

        std::string value(stringPtr);
        std::replace(value.begin(), value.end(), '"', '\'');
        result.push_back(value);
      }
      else if (it->type == "float")
      {
        std::stringstream ss;
        ss << *reinterpret_cast<float*>(val);
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
