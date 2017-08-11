#include "CSVFile.h"

#include "logger/Logger.h"


CSVFile::CSVFile(const QString & file) :
DBFile()
{
}

bool CSVFile::open()
{
  return true;
}

bool CSVFile::close()
{
  return true;
}

CSVFile::~CSVFile()
{
  close();
}

std::vector<std::string> CSVFile::get(unsigned int recordIndex, const core::TableStructure * structure) const
{
  std::vector<std::string> result;

  return result;
}
