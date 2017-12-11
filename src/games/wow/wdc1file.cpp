#include "wdc1file.h"

#include "logger/Logger.h"

#include "Game.h" // GAMEDIRECTORY Singleton

#include <sstream>

#include <bitset>

#include "WoWDatabase.h"

#define WDC1_READ_DEBUG 1

WDC1File::WDC1File(const QString & file):
WDB5File(file)
{
}

WDB5File::header WDC1File::readHeader()
{
  read(&m_header, sizeof(WDC1File::header)); // File Header

#if WDC1_READ_DEBUG > 0
  LOG_INFO << "magic" << m_header.wdb5header.magic[0] << m_header.wdb5header.magic[1] << m_header.wdb5header.magic[2] << m_header.wdb5header.magic[3];
  LOG_INFO << "record count" << m_header.wdb5header.record_count;
  LOG_INFO << "field count" << m_header.wdb5header.field_count;
  LOG_INFO << "record size" << m_header.wdb5header.record_size;
  LOG_INFO << "string table size" << m_header.wdb5header.string_table_size;
  LOG_INFO << "layout hash" << m_header.wdb5header.layout_hash;
  LOG_INFO << "min id" << m_header.wdb5header.min_id;
  LOG_INFO << "max id" << m_header.wdb5header.max_id;
  LOG_INFO << "locale" << m_header.wdb5header.locale;
  LOG_INFO << "copy table size" << m_header.wdb5header.copy_table_size;
  LOG_INFO << "flags" << m_header.wdb5header.flags;
  LOG_INFO << "id index" << m_header.wdb5header.id_index;
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

  return m_header.wdb5header;
}

bool WDC1File::open()
{
  if (!CASCFile::open())
  {
    LOG_ERROR << "An error occured while trying to read the DBCFile" << fullname();
    return false;
  }

  WDB5File::header header = readHeader();

  recordSize = header.record_size;
  recordCount = header.record_count;
  fieldCount = header.field_count;

  field_structure * fieldStructure = new field_structure[fieldCount];
  read(fieldStructure, fieldCount * sizeof(field_structure));
#if WDC1_READ_DEBUG > 0
  LOG_INFO << "--------------------------";
#endif
  for (uint i = 0; i < fieldCount; i++)
  {
#if WDC1_READ_DEBUG > 0
    LOG_INFO << "offset" << fieldStructure[i].offset << "size :" << fieldStructure[i].size << "->" << (32 - fieldStructure[i].size) / 8 << "bytes";
#endif
    m_fieldSizes[fieldStructure[i].offset] = fieldStructure[i].size;
  }
#if WDC1_READ_DEBUG > 0
  LOG_INFO << "--------------------------";
#endif

  stringSize = header.string_table_size;

  data = getPointer();
  stringTable = data + recordSize*recordCount;

  seekRelative(recordSize*recordCount + stringSize);
  if ((header.flags & 0x01) == 0)
  {
    uint32 * vals = new uint32[recordCount];
    read(vals, recordCount * sizeof(uint32));
    m_IDs.assign(vals, vals + recordCount);
    delete[] vals;

    // store offsets
    for (uint i = 0; i < recordCount; i++)
      m_recordOffsets.push_back(data + (i*recordSize));
  }
  else
  {
    LOG_ERROR << __FUNCTION__ << __LINE__ << "Not yet implemented";
    return false;
  }

  
  return true;
}

bool WDC1File::close()
{
  return WDB5File::close();
}

std::vector<std::string> WDC1File::get(unsigned int recordIndex, const core::TableStructure * structure) const
{
  std::vector<std::string> result = WDB5File::get(recordIndex, structure);

  return result;
}

WDC1File::~WDC1File()
{
  close();
}
