#pragma once

#include "types.h"
#include "wdc3file.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WDC5FILE_API_ __declspec(dllexport)
#    else
#        define _WDC5FILE_API_ __declspec(dllimport)
#    endif
#else
#    define _WDC5FILE_API_
#endif

// WDC5 was introduced in WoW 10.2.5 (Dragonflight).
// It adds two fields immediately after magic[4] in the file header:
//   uint32_t versionNum    (4 bytes)
//   char     schemaString  (128 bytes)
// All other structures (section header, field storage, etc.) are identical to WDC4.
class _WDC5FILE_API_ WDC5File : public WDC3File
{
public:
	explicit WDC5File(const QString& file);
	~WDC5File() = default;

protected:
	void readWDC3Header() override;
};
