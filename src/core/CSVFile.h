#ifndef CSVFILE_H
#define CSVFILE_H

#include <QString>

#include "dbfile.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _CSVFILE_API_ __declspec(dllexport)
#    else
#        define _CSVFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _CSVFILE_API_
#endif


class _CSVFILE_API_ CSVFile : public DBFile
{
public:

  explicit CSVFile(const QString & file);
  ~CSVFile();

  bool open();

  bool close();

  std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure * structure) const;

private:

};

#endif
