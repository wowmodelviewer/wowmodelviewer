#pragma once

#include <string>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _NPCINFOS_API_ __declspec(dllexport)
#    else
#        define _NPCINFOS_API_ __declspec(dllimport)
#    endif
#else
#    define _NPCINFOS_API_
#endif

class _NPCINFOS_API_ NPCInfos
{
public:
	NPCInfos();
	~NPCInfos()
	{
	}

	int id;
	int displayId;
	int type;
	std::wstring name;
};
