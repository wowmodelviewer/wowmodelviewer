#pragma once

#include <string>

/// @brief Stores basic NPC metadata (id, display id, type, name) imported from external sources.
class NPCInfos
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
