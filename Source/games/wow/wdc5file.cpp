#include "wdc5file.h"

WDC5File::WDC5File(const QString& file) : WDC3File(file)
{
}

void WDC5File::readWDC3Header()
{
	uint32 versionNum;
	char schemaString[128];

	// Read the magic bytes ('WDC5') shared with the WDC3 header layout
	read(m_header.magic, sizeof(m_header.magic));

	// Skip the two WDC5-specific prefix fields that don't exist in WDC3/WDC4
	read(&versionNum, sizeof(versionNum));
	read(schemaString, sizeof(schemaString));

	// Read the remaining fields, which are layout-identical to the WDC3 header
	// from record_count onward (everything after magic[4])
	read(&m_header.record_count, sizeof(WDC3File::header) - sizeof(m_header.magic));
}
