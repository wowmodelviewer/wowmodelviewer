#include "database.h"
#include <sstream>

#include "wow_enums.h"
#include "logger/Logger.h"

_DATABASE_API_ ItemDatabase items;
_DATABASE_API_ std::vector<NPCRecord> npcs;

ItemRecord::ItemRecord(const std::vector<std::string>& vals)
	: id(0), itemclass(0), subclass(0), type(0), model(0), sheath(0), quality(0)
{
	if (vals.size() < 6)
		return;

	id = std::stoi(vals[0]);
	type = std::stoi(vals[2]);
	itemclass = std::stoi(vals[3]);
	subclass = std::stoi(vals[4]);
	model = 1;
	quality = 0;
	switch (std::stoi(vals[5]))
	{
	case SHEATHETYPE_MAINHAND: sheath = ATT_LEFT_BACK_SHEATH;
		break;
	case SHEATHETYPE_LARGEWEAPON: sheath = ATT_LEFT_BACK;
		break;
	case SHEATHETYPE_HIPWEAPON: sheath = ATT_LEFT_HIP_SHEATH;
		break;
	case SHEATHETYPE_SHIELD: sheath = ATT_MIDDLE_BACK_SHEATH;
		break;
	default: sheath = SHEATHETYPE_NONE; //-V1048
	}
	name = vals[1];
}

int ItemRecord::slot()
{
	switch (type)
	{
	case IT_HEAD:
		return CS_HEAD;
	case IT_SHOULDER:
		return CS_SHOULDER;
	case IT_SHIRT:
		return CS_SHIRT;
	case IT_CHEST:
	case IT_ROBE:
		return CS_CHEST;
	case IT_BELT:
		return CS_BELT;
	case IT_PANTS:
		return CS_PANTS;
	case IT_BOOTS:
		return CS_BOOTS;
	case IT_BRACERS:
		return CS_BRACERS;
	case IT_GLOVES:
		return CS_GLOVES;
	case IT_DAGGER:
	case IT_RIGHTHANDED:
	case IT_GUN:
	case IT_THROWN:
	case IT_2HANDED:
	case IT_BOW:
		return CS_HAND_RIGHT;
	case IT_SHIELD:
	case IT_LEFTHANDED:
	case IT_OFFHAND:
		return CS_HAND_LEFT;
	case IT_CAPE:
		return CS_CAPE;
	case IT_TABARD:
		return CS_TABARD;
	case IT_RINGS:
	case IT_ACCESSORY:
	case IT_QUIVER:
	case IT_AMMO:
	case IT_UNUSED:
	case IT_RELIC:
	case IT_NECK:
	default:
		return -1;
	}
}

// Alfred. prevent null items bug.
ItemDatabase::ItemDatabase()
{
	ItemRecord all;
	all.name = "---- None ----";
	all.type = IT_ALL;

	items.push_back(all);
}

const ItemRecord& ItemDatabase::getById(int id)
{
	for (auto& item : items)
	{
		if (item.id == id)
			return item;
	}
	return items[0];
}

NPCRecord::NPCRecord(const std::string& line)
	: id(0), model(0), type(0)
{
	std::vector<std::string> values;
	std::istringstream stream(line);
	std::string token;
	while (std::getline(stream, token, ','))
		values.push_back(token);

	if (values.size() <= 3)
		return;

	id = std::stoi(values[0]);
	model = std::stoi(values[1]);
	type = std::stoi(values[2]);
	name = values[3];
}

NPCRecord::NPCRecord(const std::vector<std::string>& vals)
	: id(0), model(0), type(0)
{
	if (vals.size() < 4)
		return;

	id = std::stoi(vals[0]);
	model = std::stoi(vals[1]);
	type = std::stoi(vals[2]);
	name = vals[3];
}
