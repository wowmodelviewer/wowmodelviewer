#pragma once

#include <string>
#include <vector>
#include "dbfile.h"

#define _CSVFILE_API_

class _CSVFILE_API_ CSVFile : public DBFile
{
public:
	explicit CSVFile(std::string file);
	~CSVFile();

	bool open();

	bool close();

	std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure* structure) const;

private:
	std::string m_file;
	std::vector<std::string> m_fields;
	std::vector<std::vector<std::string>> m_values;
};
