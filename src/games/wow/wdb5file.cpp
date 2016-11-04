#include "wdb5file.h"

#include "logger/Logger.h"

#include <sstream>

WDB5File::WDB5File(const QString & file) :
DBFile(file), fieldStructure(0)
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

  recordSize = header.record_size;
  recordCount = header.record_count;
  fieldCount = header.field_count;


  fieldStructure = new field_structure[fieldCount];
  read(fieldStructure, fieldCount * sizeof(field_structure));
  LOG_INFO << "--------------------------";
  for (uint i = 0; i < fieldCount; i++)
  {
    LOG_INFO << "pos" << fieldStructure[i].position << "size :" << fieldStructure[i].size << "->" << (32 - fieldStructure[i].size) / 8 << "bytes";
  }
  LOG_INFO << "--------------------------";


  stringSize = header.string_table_size;

  data = getPointer();
  stringTable = data + recordSize*recordCount;

  // read IDs
  seekRelative(recordSize*recordCount + stringSize);
  if ((header.flags & 0x04) != 0)
  {
    IDs = new uint32[recordCount];
    read(IDs, recordCount * sizeof(uint32));
  }

  return true;
}

WDB5File::~WDB5File()
{
  close();
}

std::vector<std::string> WDB5File::get(unsigned char * recordOffset, const GameDatabase::tableStructure & structure) const
{
  std::vector<std::string> result;
  
  uint recordIndex = (uint)(recordOffset - data) / recordSize;

  for (auto it = structure.fields.begin(), itEnd = structure.fields.end();
       it != itEnd;
       ++it)
  {
    //LOG_INFO << "Reading field" << it->name;
    if (it->isKey)
    {
      //LOG_INFO << "is key";
      // Todo : manage case where id is a real field, not a separated table
      std::stringstream ss;
      ss << IDs[recordIndex];
    //  LOG_INFO << "value" << ss.str().c_str();
      result.push_back(ss.str());
      continue;
    }


    int fieldSize = (32 - fieldStructure[it->id-1].size) / 8;
    unsigned char * val = new unsigned char[fieldSize];
    memcpy(val, recordOffset + fieldStructure[it->id-1].position, fieldSize);
    
    if (it->type == "text")
    {
    //  LOG_INFO << "field type = text";
      std::string value(reinterpret_cast<char *>(stringTable + *reinterpret_cast<int *>(val)));
      std::replace(value.begin(), value.end(), '"', '\'');
//      LOG_INFO << "value" << value.c_str();
      result.push_back(value);
    }
    else if (fieldSize == 2)
    {
 //     LOG_INFO << "fieldsize == 2";
      std::stringstream ss;
      ss << *reinterpret_cast<short*>(val);
   //   LOG_INFO << "value" << ss.str().c_str();
      result.push_back(ss.str());
    }
    else
    {
 //     LOG_INFO << "fieldsize != 2";
      std::stringstream ss;
      ss << *reinterpret_cast<int*>(val);
 //     LOG_INFO << "value" << ss.str().c_str();
      result.push_back(ss.str());
    }
  }

  return result;
}
