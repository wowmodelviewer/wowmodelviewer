#ifndef CSVFILE_H
#define CSVFILE_H

#include "dbfile.h"

class CSVFile : public DBFile
{
public:

  explicit CSVFile(const QString & file);
  ~CSVFile();

  bool open();

  bool close();

  std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure * structure) const;

private:
  QString m_file;
  std::vector<QString> m_fields;
  std::vector<std::vector<std::string> > m_values;

};

#endif
