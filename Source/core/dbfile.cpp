#include "dbfile.h"

DBFile::DBFile() : recordSize(0), recordCount(0), fieldCount(0),
                   stringSize(0), data(nullptr), stringTable(nullptr)
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
