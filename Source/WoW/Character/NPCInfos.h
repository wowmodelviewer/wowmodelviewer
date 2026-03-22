#pragma once

#include <string>

#define _NPCINFOS_API_

/// @brief Stores basic NPC metadata (id, display id, type, name) imported from external sources.
class _NPCINFOS_API_ NPCInfos
{
public:
	NPCInfos();
	~NPCInfos()
	{
	}

	int id;              ///< NPC identifier.
	int displayId;       ///< Creature display info ID.
	int type;            ///< NPC type.
	std::wstring name;   ///< Display name (wide string).
};
