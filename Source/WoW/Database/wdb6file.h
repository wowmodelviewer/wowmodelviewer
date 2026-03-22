#pragma once

#include <map>
#include "types.h"
#include "wdb5file.h"

#define _WDB6FILE_API_

/// @brief Reader for the WDB6 database file format (.db2).
///
/// WDB6 extends WDB5 by adding a nonzero-column table and common-data
/// compression, reducing file sizes by storing default values separately.
class _WDB6FILE_API_ WDB6File : public WDB5File
{
public:
	struct header
	{
		WDB5File::header wdb5header;
		uint32 total_field_count;
		// new in WDB6, includes columns only expressed in the 'nonzero_column_table', unlike field_count
		uint32 nonzero_column_table_size; // new in WDB6, size of new block called 'nonzero_column_table'
	};

	explicit WDB6File(const std::string& file);
	~WDB6File();

	bool open();

	bool close();

	WDB5File::header readHeader();

	std::vector<std::string> get(unsigned int recordIndex, const core::TableStructure* structure) const;

private:
	header m_header;

	// Common data values => map[column id] => (tuple(map[id] => value (raw), type))
	std::map<uint32, std::tuple<std::map<uint32, uint32>, uint8>> m_commonData;
};
