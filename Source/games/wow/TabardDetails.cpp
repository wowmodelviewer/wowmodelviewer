#include "TabardDetails.h"
#include "Game.h"
#include <QXmlStreamWriter>

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

void TabardDetails::save(QXmlStreamWriter& stream)
{
	stream.writeStartElement("TabardDetails");

	stream.writeStartElement("Icon");
	stream.writeAttribute("value", QString::number(iconId));
	stream.writeEndElement();

	stream.writeStartElement("IconColor");
	stream.writeAttribute("value", QString::number(iconColor));
	stream.writeEndElement();

	stream.writeStartElement("Border");
	stream.writeAttribute("value", QString::number(borderId));
	stream.writeEndElement();

	stream.writeStartElement("BorderColor");
	stream.writeAttribute("value", QString::number(borderColor));
	stream.writeEndElement();

	stream.writeStartElement("Background");
	stream.writeAttribute("value", QString::number(backgroundId));
	stream.writeEndElement();

	stream.writeEndElement(); // TabardDetails
}

void TabardDetails::load(QXmlStreamReader& reader)
{
	int nbValuesRead = 0;
	while (!reader.atEnd() && nbValuesRead != 5)
	{
		if (reader.isStartElement())
		{
			if (reader.name() == "Icon")
			{
				iconId = reader.attributes().value("value").toString().toInt();
				nbValuesRead++;
			}

			if (reader.name() == "IconColor")
			{
				iconColor = reader.attributes().value("value").toString().toInt();
				nbValuesRead++;
			}

			if (reader.name() == "Border")
			{
				borderId = reader.attributes().value("value").toString().toInt();
				nbValuesRead++;
			}

			if (reader.name() == "BorderColor")
			{
				borderColor = reader.attributes().value("value").toString().toInt();
				nbValuesRead++;
			}

			if (reader.name() == "Background")
			{
				backgroundId = reader.attributes().value("value").toString().toInt();
				nbValuesRead++;
			}
		}
		reader.readNext();
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
