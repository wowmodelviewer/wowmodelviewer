#pragma once

#include <QString>
#include "dbfile.h"

#define _CSVFILE_API_

class _CSVFILE_API_ CSVFile : public DBFile
{
public:
	explicit CSVFile(QString file);
	~CSVFile();

	bool open();

	bool close();

	std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure* structure) const;

private:
	QString m_file;
	std::vector<QString> m_fields;
	std::vector<std::vector<std::string>> m_values;
};
