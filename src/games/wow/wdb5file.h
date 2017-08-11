#ifndef WDB5FILE_H
#define WDB5FILE_H

#include <QString>

#include "dbfile.h"
#include "types.h"

#include "CASCFile.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WDB5FILE_API_ __declspec(dllexport)
#    else
#        define _WDB5FILE_API_ __declspec(dllimport)
#    endif
#else
#    define _DBCFILE_API_
#endif


class _WDB5FILE_API_ WDB5File : public DBFile, public CASCFile
{
public:

  struct header
  {
    char magic[4];                                               // 'WDB5' for .db2 (database)
    uint32 record_count;
    uint32 field_count;                                         // for the first time, this counts arrays as '1'; in the past, only the WCH* variants have counted arrays as 1 field
    uint32 record_size;
    uint32 string_table_size;                                   // if flags & 0x01 != 0, this field takes on a new meaning - it becomes an absolute offset to the beginning of the offset_map
    uint32 table_hash;
    uint32 layout_hash;                                         // used to be 'build', but after build 21737, this is a new hash field that changes only when the structure of the data changes
    uint32 min_id;
    uint32 max_id;
    uint32 locale;                                              // as seen in TextWowEnum
    uint32 copy_table_size;
    uint16 flags;                                               // in WDB3/WCH4, this field was in the WoW executable's DBCMeta; possible values are listed in Known Flag Meanings
    uint16 id_index;                                            // new in WDB5 (and only after build 21737), this is the index of the field containing ID values; this is ignored if flags & 0x04 != 0
  };

  explicit WDB5File(const QString & file);
  ~WDB5File();

  virtual bool open();

  virtual bool close();

  virtual header readHeader();

  virtual std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure * structure) const;

protected:
  std::vector<uint32> m_IDs;

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
  std::vector<unsigned char *> m_recordOffsets;

  bool m_isSparseTable;
};

#endif
