#include "dbfile.h"

#include <sstream>

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
	unsigned int na,nb,es,ss;

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

	read(&na,4); // Number of records
	read(&nb,4); // Number of fields
	read(&es,4); // Size of a record
	read(&ss,4); // String size

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
	assert(fieldCount*4 >= recordSize);

	if (db_type == FT_WDB2) {
		seek(getSize() - recordSize*recordCount - stringSize);
	}

  data = getPointer();
  stringTable = data + recordSize*recordCount;

	return true;
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


std::vector<std::string> DBFile::Record::get(const std::map<int, std::pair<QString, QString> > & structure) const
{
  std::vector<std::string> result;
  unsigned int offset = 0; // to handle byte reading, incremented each time a byte member is read
  for(std::map<int, std::pair<QString, QString> >::const_iterator it = structure.begin(), itEnd = structure.end();
      it != itEnd ;
      ++it)
  {
   // std::cout << it->second.first << " => ";
    if(it->second.second == "uint")
    {
     // std::cout << "uint => " << it->first << " => ";
      std::stringstream ss;
      ss << getUInt(it->first);
      std::string field = ss.str();
     // std::cout << field << std::endl;
      result.push_back(field);
    }
    else if(it->second.second == "int")
    {
      // std::cout << "uint => " << it->first << " => ";
      std::stringstream ss;
      ss << getInt(it->first);
      std::string field = ss.str();
      // std::cout << field << std::endl;
      result.push_back(field);
    }
    else if(it->second.second == "text")
    {
      // as " character cause weird issues with sql queries, replace it with '
      std::string val = getStdString(it->first);
      std::replace(val.begin(),val.end(),'"','\'');
      result.push_back(val);
    }
    else if(it->second.second == "float")
    {
     // std::cout << "float => " << it->first << " => ";
      std::stringstream ss;
      ss << getFloat(it->first);
      std::string field = ss.str();
     // std::cout << field << std::endl;
      result.push_back(field);
    }
    else if(it->second.second == "byte")
    {
    	unsigned int decal = 0;
    	switch(offset)
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
    	unsigned int val = getUInt(it->first-offset);
    	ss << ((val>>decal)&0x000000FF);
    	std::string field = ss.str();
    	// std::cout << field << std::endl;
    	result.push_back(field);
    	offset++;
    }
  }
  return result;
}

DBFile::Iterator DBFile::begin()
{
	//assert(data);
	return Iterator(*this, data);
}
DBFile::Iterator DBFile::end()
{
	//assert(data);
	return Iterator(*this, stringTable);
}

