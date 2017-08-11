#include "wdb6file.h"

#include "logger/Logger.h"

#include "Game.h" // GAMEDIRECTORY Singleton

#include <sstream>

#include <bitset>

#include "WoWDatabase.h"

#define WDB6_READ_DEBUG 0

WDB6File::WDB6File(const QString & file) :
WDB5File(file)
{
}

WDB5File::header WDB6File::readHeader()
{
  read(&m_header, sizeof(WDB6File::header)); // File Header

#if WDB6_READ_DEBUG > 0
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
  LOG_INFO << "nonzero_column_table_size" << m_header.nonzero_column_table_size;
#endif

  return m_header.wdb5header;
}

bool WDB6File::open()
{
  if (!WDB5File::open())
    return false;

  // reading common data table values
  if (m_header.nonzero_column_table_size > 0)
  {
    uint32 nbcolumns;
    read(&nbcolumns, sizeof(nbcolumns));

    for (uint c = 0; c < nbcolumns; c++)
    {
      // read number of records
      uint32 nbrecords;
      read(&nbrecords, sizeof(nbrecords));

      // read type
      uint8 type;
      read(&type, sizeof(type));

      if (nbrecords == 0)
        continue;

      std::map<uint32, uint32> values;

      uint32 size = 4;

      // starting from 7.3 version, data in common data is stored in 4 bytes, not dynamic size anymore
      if (!GAMEDIRECTORY.version().contains("7.3"))
      {
        if (type == 1)
          size = 2;
        else if (type == 2)
          size = 1;
      }

      for (uint i = 0; i < nbrecords; i++)
      {
        uint32 id;
        read(&id, sizeof(id));

        uint32 val = 0;
        read(&val, size);

        values[id] = val;
      }

      std::tuple<std::map<uint32, uint32>, uint8> column = std::make_tuple(values, type);
      m_commonData[c] = column;

    }
  }
  
  return true;
}

bool WDB6File::close()
{
  return WDB5File::close();
}

std::vector<std::string> WDB6File::get(unsigned int recordIndex, const core::TableStructure * structure) const
{
  std::vector<std::string> result = WDB5File::get(recordIndex, structure);

  for (auto it = structure->fields.begin(), itEnd = structure->fields.end();
       it != itEnd;
       ++it)
  {
    wow::FieldStructure * field = dynamic_cast<wow::FieldStructure *>(*it);

    if (!field)
      continue;

    if (field->isCommonData)
    {
      auto common = m_commonData.find(field->pos);
      if (common != m_commonData.end())
      {
        auto val = std::get<0>(common->second).find(m_IDs[recordIndex]);
        if (val != std::get<0>(common->second).end())
        {
          uint8 type = std::get<1>(common->second);
          if (type == 1)
          {
            std::stringstream ss;
            ss << static_cast<short>(val->second);
            result.push_back(ss.str());
          }
          else if (type == 2)
          {
            std::stringstream ss;
            ss << (static_cast<unsigned int>(val->second) & 0x000000FF);
            result.push_back(ss.str());
          }
          else if (type == 3)
          {
            std::stringstream ss;
            ss << static_cast<float>(val->second);
            result.push_back(ss.str());
          }
          else if (type == 4)
          {
            std::stringstream ss;
            ss << static_cast<int>(val->second);
            result.push_back(ss.str());
          }
        }
        else // if no value defined, insert 0
        {
          result.push_back("0");
        }
      }
    }
  }


  return result;
}

WDB6File::~WDB6File()
{
  close();
}
