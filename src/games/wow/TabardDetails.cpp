/*
 * TabardDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "TabardDetails.h"

#include "GameDirectory.h"
#include "wow_enums.h"

#include <QXmlStreamWriter>

const std::vector<QString> TabardDetails::ICON_COLOR_VECTOR =
      {"ff092a5d", "ffffffff", "ff004867", "ff516700", "ff5d094f", "ff54370a", "ff00671f", "ff808080",
       "ff006757", "ff636700",  "ff672300", "ff670021", "ff101517", "ffdfa55a", "ffb1b8b1", "ff376700"};


const std::vector<QString> TabardDetails::BORDER_COLOR_VECTOR =
      {"ff670021", "ff006793", "ffffffff", "ff675600", "fff9cc30", "ff54370a", "ff674500", "ff0f1415",
       "ff63a300", "ff00317c", "ff00671f", "ff639400", "ff7b0067", "ff6d0077", "ff008e90", "ff672300"};

const std::vector<QString> TabardDetails::BACKGROUND_COLOR_VECTOR =
      {"ffd7ddcb", "ffff891b", "ff009061", "ffd34ac8", "ffae4b00", "ff21dcff", "ffbcf61b", "ff88ba03",
       "ff1ef7c1", "ff860f9a", "ff2c6aae", "ffff2088", "ff808080", "ff00820f", "ff9b00a6", "ffff1fbf",
       "ffe14500", "ffad29ac", "ffc49b00", "ff588000", "ff8e9700", "ff1eff68", "ffc900c3", "ff009dc5",
       "fffffc14", "ff4d8eda", "fff68700", "ffe3f618", "ffb7c003", "ffffff14", "ffbd005b", "ff04c347",
       "ffc58132", "ffd30087", "ff04b78f", "ffd8dd00", "fff3ca00", "ffb1002e", "ff9e0036", "ff006391",
       "ff232323", "ffffffff", "ff003582", "ffff38fa", "ff4f2300", "ff646464", "ffa30068", "ffffb317",
       "fffc6891", "ffb4bba8", "ff875513"};


unsigned int TabardDetails::iconColorToIndex(QString & color)
{
  unsigned int result = 0;

  for(;result < ICON_COLOR_VECTOR.size() ; result++)
  {
    if(color == ICON_COLOR_VECTOR[result])
      break;
  }

  return result;
}

unsigned int TabardDetails::borderColorToIndex(QString & color)
{
  unsigned int result = 0;

  for(;result < BORDER_COLOR_VECTOR.size() ; result++)
  {
    if(color == BORDER_COLOR_VECTOR[result])
      break;
  }

  return result;
}

unsigned int TabardDetails::backgroundColorToIndex(QString & color)
{
  unsigned int result = 0;

  for(;result < BACKGROUND_COLOR_VECTOR.size() ; result++)
  {
    if(color == BACKGROUND_COLOR_VECTOR[result])
      break;
  }

  return result;
}


QString TabardDetails::GetBackgroundTex(int slot)
{
	if (slot == CR_TORSO_UPPER)
		return QString("Textures\\GuildEmblems\\Background_%1_TU_U.blp").arg(Background,2,10,QChar('0'));
	else
		return QString("Textures\\GuildEmblems\\Background_%1_TL_U.blp").arg(Background,2,10,QChar('0'));
}

QString TabardDetails::GetBorderTex(int slot)
{
	if (slot == CR_TORSO_UPPER)
		return QString("Textures\\GuildEmblems\\Border_%1_%2_TU_U.blp").arg(Border,2,10,QChar('0')).arg(BorderColor,2,10,QChar('0'));
	else
		return QString("Textures\\GuildEmblems\\Border_%1_%2_TL_U.blp").arg(Border,2,10,QChar('0')).arg(BorderColor,2,10,QChar('0'));
}

QString TabardDetails::GetIconTex(int slot)
{
	if(slot == CR_TORSO_UPPER)
		return QString("Textures\\GuildEmblems\\Emblem_%1_%2_TU_U.blp").arg(Icon,2,10,QChar('0')).arg(IconColor,2,10,QChar('0'));
	else
		return QString("Textures\\GuildEmblems\\Emblem_%1_%2_TL_U.blp").arg(Icon,2,10,QChar('0')).arg(IconColor,2,10,QChar('0'));
}

int TabardDetails::GetMaxBackground()
{
	int i = 0;
	while(GAMEDIRECTORY.fileExists(QString("Textures\\GuildEmblems\\Background_%1_TU_U.blp").arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

int TabardDetails::GetMaxIcon()
{
	int i = 0;
	while(GAMEDIRECTORY.fileExists(QString("Textures\\GuildEmblems\\Emblem_%1_00_TU_U.blp").arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

int TabardDetails::GetMaxIconColor(int icon)
{
	int i = 0;
	while(GAMEDIRECTORY.fileExists(QString("Textures\\GuildEmblems\\Emblem_%1_%2_TU_U.blp").arg(icon,2,10,QChar('0')).arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

int TabardDetails::GetMaxBorder()
{
	int i = 0;
	while(GAMEDIRECTORY.fileExists(QString("Textures\\GuildEmblems\\Border_%1_00_TU_U.blp").arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

int TabardDetails::GetMaxBorderColor(int border)
{
	int i = 0;
	while(GAMEDIRECTORY.fileExists(QString("Textures\\GuildEmblems\\Border_%1_%2_TU_U.blp").arg(border,2,10,QChar('0')).arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

void TabardDetails::save(QXmlStreamWriter & stream)
{
  stream.writeStartElement("TabardDetails");

  stream.writeStartElement("Icon");
  stream.writeAttribute("value", QString::number(Icon));
  stream.writeEndElement();

  stream.writeStartElement("IconColor");
  stream.writeAttribute("value", QString::number(IconColor));
  stream.writeEndElement();

  stream.writeStartElement("Border");
  stream.writeAttribute("value", QString::number(Border));
  stream.writeEndElement();

  stream.writeStartElement("BorderColor");
  stream.writeAttribute("value", QString::number(BorderColor));
  stream.writeEndElement();

  stream.writeStartElement("Background");
  stream.writeAttribute("value", QString::number(Background));
  stream.writeEndElement();

  stream.writeEndElement(); // TabardDetails
}

void TabardDetails::load(QXmlStreamReader & reader)
{
  int nbValuesRead = 0;
  while (!reader.atEnd() && nbValuesRead != 5)
  {
    if (reader.isStartElement())
    {
      if(reader.name() == "Icon")
      {
        Icon = reader.attributes().value("value").toString().toInt();
        nbValuesRead++;
      }

      if(reader.name() == "IconColor")
      {
        IconColor = reader.attributes().value("value").toString().toInt();
        nbValuesRead++;
      }

      if(reader.name() == "Border")
      {
        Border = reader.attributes().value("value").toString().toInt();
        nbValuesRead++;
      }

      if(reader.name() == "BorderColor")
      {
        BorderColor = reader.attributes().value("value").toString().toInt();
        nbValuesRead++;
      }

      if(reader.name() == "Background")
      {
        Background = reader.attributes().value("value").toString().toInt();
        nbValuesRead++;
      }
    }
    reader.readNext();
  }
}
