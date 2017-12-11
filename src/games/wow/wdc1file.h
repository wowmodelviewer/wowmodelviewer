#ifndef WDC1FILE_H
#define WDC1FILE_H

#include <QString>

#include "types.h"
#include "wdb5file.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WDC1FILE_API_ __declspec(dllexport)
#    else
#        define _WDC1FILE_API_ __declspec(dllimport)
#    endif
#else
#    define _WDC1FILE_API_
#endif


class _WDC1FILE_API_ WDC1File : public WDB5File
{
public:

  struct header
  {
    WDB5File::header wdb5header;
    uint32 total_field_count;      // in WDC1 this value seems to always be the same as the 'field_count' value
    uint32 bitpacked_data_offset;  // relative position in record where bitpacked data begins; not important for parsing the file
    uint32 lookup_column_count;
    uint32 offset_map_offset;      // Offset to array of struct {uint32_t offset; uint16_t size;}[max_id - min_id + 1];
    uint32 id_list_size;           // List of ids present in the DB file
    uint32 field_storage_info_size;
    uint32 common_data_size;
    uint32 pallet_data_size;
    uint32 relationship_data_size;
  };

  explicit WDC1File(const QString & file);
  ~WDC1File();

  bool open();

  bool close();

  WDB5File::header readHeader();

  std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure * structure) const;

private:
  struct field_structure
  {
    uint16 size;
    uint16 offset;
  };

  header m_header;

};

#endif
