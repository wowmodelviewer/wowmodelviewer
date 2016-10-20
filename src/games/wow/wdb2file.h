#ifndef WDB2FILE_H
#define WDB2FILE_H

#include <QString>

#include "DBFile.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WDB2FILE_API_ __declspec(dllexport)
#    else
#        define _WDB2FILE_API_ __declspec(dllimport)
#    endif
#else
#    define _WDB2FILE_API_
#endif


class _WDB2FILE_API_ WDB2File : public DBFile
{
public:
  explicit WDB2File(const QString & file);
  ~WDB2File();

	// Open database. It must be openened before it can be used.
  bool doSpecializedOpen();

  std::vector<std::string> get(unsigned char * recordOffset, const std::map<int, std::pair<QString, QString> > & structure) const;

private:
};

#endif
