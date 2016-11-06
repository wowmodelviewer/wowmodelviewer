#include "dbfile.h"

#include "logger/Logger.h"

DBFile::DBFile(const QString & file) : 
  CASCFile(file),
  data(0),
  stringTable(0),
  recordSize(0),
  recordCount(0),
  fieldCount(0),
  stringSize(0)
{
}

bool DBFile::open()
{
  if (!CASCFile::open())
  {
    LOG_ERROR << "An error occured while trying to read the DBCFile" << fullname();
    return false;
  }

  return doSpecializedOpen();
}

bool DBFile::close()
{
  CASCFile::close();
  return true;
}

DBFile::~DBFile()
{
  close();
}

DBFile::Iterator DBFile::begin()
{
	return Iterator(*this, 0);
}
DBFile::Iterator DBFile::end()
{
	return Iterator(*this, recordCount);
}

