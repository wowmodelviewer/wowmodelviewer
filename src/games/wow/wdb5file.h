#ifndef WDB5FILE_H
#define WDB5FILE_H

#include <QString>

#include "dbfile.h"
#include "types.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WDB5FILE_API_ __declspec(dllexport)
#    else
#        define _WDB5FILE_API_ __declspec(dllimport)
#    endif
#else
#    define _DBCFILE_API_
#endif


class _WDB5FILE_API_ WDB5File : public DBFile
{
public:
  explicit WDB5File(const QString & file);
  ~WDB5File();

	// Open database. It must be openened before it can be used.
  bool doSpecializedOpen();

  std::vector<std::string> get(unsigned int recordIndex, const GameDatabase::tableStructure & structure) const;

private:
  struct field_structure
  {
    int16 size;
    uint16 position;
  };

  struct copy_table_entry
  {
    uint32 newRowId;
    uint32 copiedRowId;
  };

  std::map<int, int> m_fieldSizes;
  std::vector<uint32> m_IDs;
  std::vector<unsigned char *> m_recordOffsets;

  bool m_isSparseTable;
};

#endif
