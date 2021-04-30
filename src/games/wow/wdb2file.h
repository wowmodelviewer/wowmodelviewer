#ifndef WDB2FILE_H
#define WDB2FILE_H

#include "dbfile.h"

#include "CASCFile.h"

class WDB2File : public DBFile, public CASCFile
{
public:
  explicit WDB2File(const QString & file);
  ~WDB2File();

  bool open();

  bool close();

  float getFloat(unsigned char * recordOffset, size_t field) const
  {
    return *reinterpret_cast<float*>(recordOffset + field * 4);
  }
  unsigned int getUInt(unsigned char * recordOffset, size_t field) const
  {
    return *reinterpret_cast<unsigned int*>(recordOffset + (field * 4));
  }
  int getInt(unsigned char * recordOffset, size_t field) const
  {
    return *reinterpret_cast<int*>(recordOffset + field * 4);
  }
  unsigned char getByte(unsigned char * recordOffset, size_t ofs) const
  {
    return *reinterpret_cast<unsigned char*>(recordOffset + ofs);
  }

  std::string getStdString(unsigned char * recordOffset, size_t field) const
  {
    size_t stringOffset = getUInt(recordOffset, field);
    if (stringOffset >= stringSize)
      stringOffset = 0;

    return std::string(reinterpret_cast<char*>(stringTable + stringOffset));
  }

  std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure * structure) const;

private:
};

#endif
