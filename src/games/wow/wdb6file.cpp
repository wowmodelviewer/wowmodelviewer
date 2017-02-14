#include "wdb6file.h"

#include "logger/Logger.h"

#include <sstream>

#include <bitset>

#define WDB6_READ_DEBUG 1

WDB6File::WDB6File(const QString & file) :
WDB5File(file)
{
}

WDB5File::header WDB6File::readHeader()
{
  read(&m_header, sizeof(WDB6File::header)); // File Header

#if WDB6_READ_DEBUG > 0
  LOG_INFO << "magic" << m_header.wdb5header.magic[0] << m_header.wdb5header.magic[1] << m_header.wdb5header.magic[2] << m_header.wdb5header.magic[3];
  LOG_INFO << "record count" << m_header.wdb5header.record_count;
  LOG_INFO << "field count" << m_header.wdb5header.field_count;
  LOG_INFO << "record size" << m_header.wdb5header.record_size;
  LOG_INFO << "string table size" << m_header.wdb5header.string_table_size;
  LOG_INFO << "layout hash" << m_header.wdb5header.layout_hash;
  LOG_INFO << "min id" << m_header.wdb5header.min_id;
  LOG_INFO << "max id" << m_header.wdb5header.max_id;
  LOG_INFO << "locale" << m_header.wdb5header.locale;
  LOG_INFO << "copy table size" << m_header.wdb5header.copy_table_size;
  LOG_INFO << "flags" << m_header.wdb5header.flags;
  LOG_INFO << "id index" << m_header.wdb5header.id_index;
  LOG_INFO << "total_field_count" << m_header.total_field_count;
  LOG_INFO << "nonzero_column_table_size" << m_header.nonzero_column_table_size;
#endif

  return m_header.wdb5header;
}

bool WDB6File::doSpecializedOpen()
{
  return WDB5File::doSpecializedOpen();
}

WDB6File::~WDB6File()
{
  close();
}
