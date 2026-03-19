#pragma once

#include <string>

#define _NPCINFOS_API_

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
