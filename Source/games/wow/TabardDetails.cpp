#include "TabardDetails.h"
#include "Game.h"
#include "WoWDatabase.h"
#include "DB2Table.h"

#include <set>

TabardDetails::TabardDetails()
	: showCustom(false),
	  iconId(0),
	  iconColor(0),
	  borderId(0),
	  borderColor(0),
	  backgroundId(0),
	  tier(0)
{
	if (const DB2Table* tbl = WOWDB.getTable("GuildTabardBackground"))
	{
		std::set<int> unique;
		for (const auto& row : *tbl)
			unique.insert(static_cast<int>(row.getUInt("Color")));
		backgrounds.assign(unique.begin(), unique.end());
	}

	if (const DB2Table* tbl = WOWDB.getTable("GuildTabardEmblem"))
	{
		std::set<int> unique;
		for (const auto& row : *tbl)
			unique.insert(static_cast<int>(row.getUInt("EmblemID")));
		icons.assign(unique.begin(), unique.end());
	}

	if (const DB2Table* tbl = WOWDB.getTable("GuildTabardBorder"))
	{
		std::set<int> unique;
		for (const auto& row : *tbl)
			unique.insert(static_cast<int>(row.getUInt("BorderID")));
		borders.assign(unique.begin(), unique.end());
	}
}

GameFile* TabardDetails::GetBackgroundTex(int slot)
{
	if (const DB2Table* tbl = WOWDB.getTable("GuildTabardBackground"))
	{
		for (const auto& row : *tbl)
		{
			if (static_cast<int>(row.getUInt("Color")) == backgroundId &&
				static_cast<int>(row.getUInt("Tier")) == tier &&
				static_cast<int>(row.getUInt("Component")) == slot)
				return GAMEDIRECTORY.getFile(row.getUInt("FileDataID"));
		}
	}
	return nullptr;
}

GameFile* TabardDetails::GetBorderTex(int slot)
{
	if (const DB2Table* tbl = WOWDB.getTable("GuildTabardBorder"))
	{
		for (const auto& row : *tbl)
		{
			if (static_cast<int>(row.getUInt("BorderID")) == borderId &&
				static_cast<int>(row.getUInt("Color")) == borderColor &&
				static_cast<int>(row.getUInt("Tier")) == tier &&
				static_cast<int>(row.getUInt("Component")) == slot)
				return GAMEDIRECTORY.getFile(row.getUInt("FileDataID"));
		}
	}
	return nullptr;
}

GameFile* TabardDetails::GetIconTex(int slot)
{
	if (const DB2Table* tbl = WOWDB.getTable("GuildTabardEmblem"))
	{
		for (const auto& row : *tbl)
		{
			if (static_cast<int>(row.getUInt("EmblemID")) == iconId &&
				static_cast<int>(row.getUInt("Color")) == iconColor &&
				static_cast<int>(row.getUInt("Component")) == slot)
				return GAMEDIRECTORY.getFile(row.getUInt("FileDataID"));
		}
	}
	return nullptr;
}

int TabardDetails::GetMaxBackground()
{
	return static_cast<int>(backgrounds.size());
}

int TabardDetails::GetMaxIcon()
{
	return static_cast<int>(icons.size());
}

int TabardDetails::GetMaxIconColor(int icon)
{
	if (const DB2Table* tbl = WOWDB.getTable("GuildTabardEmblem"))
	{
		std::set<int> uniqueColors;
		for (const auto& row : *tbl)
			if (static_cast<int>(row.getUInt("EmblemID")) == icon)
				uniqueColors.insert(static_cast<int>(row.getUInt("Color")));
		return static_cast<int>(uniqueColors.size());
	}
	return -1;
}

int TabardDetails::GetMaxBorder()
{
	return static_cast<int>(borders.size());
}

int TabardDetails::GetMaxBorderColor(int border)
{
	if (const DB2Table* tbl = WOWDB.getTable("GuildTabardBorder"))
	{
		std::set<int> uniqueColors;
		for (const auto& row : *tbl)
			if (static_cast<int>(row.getUInt("BorderID")) == border)
				uniqueColors.insert(static_cast<int>(row.getUInt("Color")));
		return static_cast<int>(uniqueColors.size());
	}
	return -1;
}

void TabardDetails::save(pugi::xml_node& parentNode)
{
	pugi::xml_node node = parentNode.append_child("TabardDetails");

	node.append_child("Icon").append_attribute("value") = iconId;
	node.append_child("IconColor").append_attribute("value") = iconColor;
	node.append_child("Border").append_attribute("value") = borderId;
	node.append_child("BorderColor").append_attribute("value") = borderColor;
	node.append_child("Background").append_attribute("value") = backgroundId;
}

void TabardDetails::load(const pugi::xml_node& node)
{
	for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
	{
		const std::string name = child.name();

		if (name == "Icon")
			iconId = child.attribute("value").as_int();
		else if (name == "IconColor")
			iconColor = child.attribute("value").as_int();
		else if (name == "Border")
			borderId = child.attribute("value").as_int();
		else if (name == "BorderColor")
			borderColor = child.attribute("value").as_int();
		else if (name == "Background")
			backgroundId = child.attribute("value").as_int();
	}
}

int TabardDetails::getIcon()
{
	return std::distance(icons.begin(), std::find(icons.begin(), icons.end(), iconId));
}

int TabardDetails::getIconColor()
{
	return iconColor;
}

int TabardDetails::getBorder()
{
	return std::distance(borders.begin(), std::find(borders.begin(), borders.end(), borderId));
}

int TabardDetails::getBorderColor()
{
	return borderColor;
}

int TabardDetails::getBackground()
{
	return std::distance(backgrounds.begin(), std::find(backgrounds.begin(), backgrounds.end(), backgroundId));
}

void TabardDetails::setIcon(int icon)
{
	iconId = icons[icon];
}

void TabardDetails::setIconColor(int color)
{
	iconColor = color;
}

void TabardDetails::setBorder(int border)
{
	borderId = borders[border];
}

void TabardDetails::setBorderColor(int color)
{
	borderColor = color;
}

void TabardDetails::setBackground(int background)
{
	backgroundId = backgrounds[background];
}

void TabardDetails::setTabardId(int itemid)
{
	if (itemid == 69210) // Renowned Guild Tabard
		tier = 2;
	else if (itemid == 69209) // Illustrious Guild Tabard
		tier = 1;
	else // regular Guild Tabard
		tier = 0;
}

void TabardDetails::setIconId(int id)
{
	iconId = id;
}

void TabardDetails::setBorderId(int id)
{
	borderId = id;
}

void TabardDetails::setBackgroundId(int id)
{
	backgroundId = id;
}
