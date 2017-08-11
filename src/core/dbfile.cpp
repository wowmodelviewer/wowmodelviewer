#include "dbfile.h"

#include "logger/Logger.h"

DBFile::DBFile() :
  data(0),
  stringTable(0),
  recordSize(0),
  recordCount(0),
  fieldCount(0),
  stringSize(0)
{
}

DBFile::Iterator DBFile::begin()
{
	return Iterator(*this, 0);
}
DBFile::Iterator DBFile::end()
{
	return Iterator(*this, recordCount);
}

