#include "TabardDetails.h"
#include "Game.h"

TabardDetails::TabardDetails()
    : showCustom(false),
      iconId(0),
      iconColor(0),
      borderId(0),
      borderColor(0),
      backgroundId(0),
      tier(0)
{
	std::string query = "SELECT DISTINCT Color FROM GuildTabardBackground";
	sqlResult r = GAMEDATABASE.sqlQuery(query);

	if (r.valid && !r.values.empty())
		for (auto& v : r.values)
			backgrounds.push_back(std::stoi(v[0]));

	query = "SELECT DISTINCT EmblemID FROM GuildTabardEmblem";
	r = GAMEDATABASE.sqlQuery(query);

	if (r.valid && !r.values.empty())
		for (auto& v : r.values)
			icons.push_back(std::stoi(v[0]));

	query = "SELECT DISTINCT BorderID FROM GuildTabardBorder";
	r = GAMEDATABASE.sqlQuery(query);

	if (r.valid && !r.values.empty())
		for (auto& v : r.values)
			borders.push_back(std::stoi(v[0]));
}

GameFile* TabardDetails::GetBackgroundTex(int slot)
{
	GameFile* result = nullptr;
	const std::string query = "SELECT FileDataID FROM GuildTabardBackground WHERE Color="
		+ std::to_string(backgroundId) + " AND Tier=" + std::to_string(tier) + " AND Component=" + std::to_string(slot);

	const sqlResult r = GAMEDATABASE.sqlQuery(query);

	if (r.valid && !r.values.empty())
		result = GAMEDIRECTORY.getFile(std::stoi(r.values[0][0]));

	return result;
}

GameFile* TabardDetails::GetBorderTex(int slot)
{
	GameFile* result = nullptr;
	const std::string query = "SELECT FileDataID FROM GuildTabardBorder WHERE BorderID="
		+ std::to_string(borderId) + " AND Color=" + std::to_string(borderColor) + " AND Tier=" + std::to_string(tier) + " AND Component=" + std::to_string(slot);

	const sqlResult r = GAMEDATABASE.sqlQuery(query);

	if (r.valid && !r.values.empty())
		result = GAMEDIRECTORY.getFile(std::stoi(r.values[0][0]));

	return result;
}

GameFile* TabardDetails::GetIconTex(int slot)
{
	GameFile* result = nullptr;
	const std::string query = "SELECT FileDataID FROM GuildTabardEmblem WHERE EmblemID="
		+ std::to_string(iconId) + " AND Color=" + std::to_string(iconColor) + " AND Component=" + std::to_string(slot);

	const sqlResult r = GAMEDATABASE.sqlQuery(query);

	if (r.valid && !r.values.empty())
		result = GAMEDIRECTORY.getFile(std::stoi(r.values[0][0]));

	return result;
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
	const std::string query = "SELECT COUNT(*) FROM(SELECT DISTINCT Color FROM GuildTabardEmblem WHERE EmblemID = "
		+ std::to_string(icon) + ")";

	const sqlResult r = GAMEDATABASE.sqlQuery(query);

	if (r.valid && !r.values.empty())
		return std::stoi(r.values[0][0]);

	return -1;
}

int TabardDetails::GetMaxBorder()
{
	return static_cast<int>(borders.size());
}

int TabardDetails::GetMaxBorderColor(int border)
{
	const std::string query = "SELECT COUNT(*) FROM (SELECT DISTINCT Color FROM GuildTabardBorder WHERE BorderID = "
		+ std::to_string(border) + ")";

	const sqlResult r = GAMEDATABASE.sqlQuery(query);

	if (r.valid && !r.values.empty())
		return std::stoi(r.values[0][0]);

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
