#include "wdb2file.h"

#include "GameFile.h"
#include "types.h"

#include "logger/Logger.h"

#include <sstream>

WDB2File::WDB2File(const QString & file) :
  DBFile(file)
{
}

bool WDB2File::doSpecializedOpen()
{
  enum FileType {
    FT_UNK,
    FT_WDBC,
    FT_WDB2,
  };
  int db_type = FT_UNK;

  // Need some error checking, otherwise an unhandled exception error occurs
  // if people screw with the data path.

  //if (f->isEof())
  //return false;

  char header[5];
  unsigned int na, nb, es, ss;

  read(header, 4); // File Header
  if (strncmp(header, "WDBC", 4) == 0)
    db_type = FT_WDBC;
  else if (strncmp(header, "WDB2", 4) == 0)
    db_type = FT_WDB2;

  if (db_type == FT_UNK) {
    close();
    data = NULL;
    LOG_ERROR << "An error occured while trying to read the DBCFile" << fullname() << "header:" << header[0] << header[1] << header[2] << header[3];
    return false;
  }

  //assert(header[0]=='W' && header[1]=='D' && header[2]=='B' && header[3] == 'C');

  read(&na, 4); // Number of records
  read(&nb, 4); // Number of fields
  read(&es, 4); // Size of a record
  read(&ss, 4); // String size

  if (db_type == FT_WDB2) {
    seekRelative(28);
    // just some buggy check
    unsigned int check;
    read(&check, 4);
    if (check == 6) // wrong place
      seekRelative(-20);
    else // check == 17, right place
      seekRelative(-4);
  }

  recordSize = es;
  recordCount = na;
  fieldCount = nb;
  stringSize = ss;
  //assert(fieldCount*4 == recordSize);
  // not always true, but it works fine till now
  assert(fieldCount * 4 >= recordSize);

  if (db_type == FT_WDB2) {
    seek(getSize() - recordSize*recordCount - stringSize);
  }

  data = getPointer();
  stringTable = data + recordSize*recordCount;

  return true;
}


WDB2File::~WDB2File()
{
  close();
}

std::vector<std::string> WDB2File::get(unsigned int recordIndex, const WoWDatabase::tableStructure & structure) const
{
  unsigned char * recordOffset = data + (recordIndex * recordSize);
  std::vector<std::string> result;
  unsigned int offset = 0; // to handle byte reading, incremented each time a byte member is read
  for (auto it = structure.fields.begin(), itEnd = structure.fields.end();
       it != itEnd;
       ++it)
  {
    // std::cout << it->second.first << " => ";
    if (it->type == "uint")
    {
      // std::cout << "uint => " << it->first << " => ";
      std::stringstream ss;
      ss << getUInt(recordOffset, it->id);
      std::string field = ss.str();
      // std::cout << field << std::endl;
      result.push_back(field);
    }
    else if (it->type == "int")
    {
      // std::cout << "uint => " << it->first << " => ";
      std::stringstream ss;
      ss << getInt(recordOffset, it->id);
      std::string field = ss.str();
      // std::cout << field << std::endl;
      result.push_back(field);
    }
    else if (it->type == "text")
    {
      // as " character cause weird issues with sql queries, replace it with '
      std::string val = getStdString(recordOffset, it->id);
      std::replace(val.begin(), val.end(), '"', '\'');
      result.push_back(val);
    }
    else if (it->type == "float")
    {
      // std::cout << "float => " << it->first << " => ";
      std::stringstream ss;
      ss << getFloat(recordOffset, it->id);
      std::string field = ss.str();
      // std::cout << field << std::endl;
      result.push_back(field);
    }
    else if (it->type == "byte")
    {
      unsigned int decal = 0;
      switch (offset)
      {
        case 0:
          decal = 24;
          break;
        case 1:
          decal = 16;
          break;
        case 2:
          decal = 8;
          break;
        default:
          decal = 0;
          break;
      }

      std::stringstream ss;
      unsigned int val = getUInt(recordOffset, it->id - offset);
      ss << ((val >> decal) & 0x000000FF);
      std::string field = ss.str();
      // std::cout << field << std::endl;
      result.push_back(field);
      offset++;
    }
  }
  return result;
}