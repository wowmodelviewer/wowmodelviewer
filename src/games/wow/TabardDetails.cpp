/*
 * TabardDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "TabardDetails.h"

#include "Game.h"
#include "wow_enums.h"

#include <QXmlStreamWriter>

TabardDetails::TabardDetails()
  : maxIcon(-1), maxBorder(-1), maxBackground(-1)
{

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
  if (maxBackground == -1)
  {
    int i = 0;
    while (GAMEDIRECTORY.getFile(QString("Textures\\GuildEmblems\\Background_%1_TU_U.blp").arg(i, 2, 10, QChar('0'))))
      i++;
    maxBackground = i;
  }
	return maxBackground;
}

int TabardDetails::GetMaxIcon()
{
  if (maxIcon == -1)
  {
    int i = 0;
    while (GAMEDIRECTORY.getFile(QString("Textures\\GuildEmblems\\Emblem_%1_00_TU_U.blp").arg(i, 2, 10, QChar('0'))))
      i++;
    maxIcon = i;
  }
	return maxIcon;
}

int TabardDetails::GetMaxIconColor(int icon)
{
  auto it = maxIconColorMap.find(icon);

  if (it == maxIconColorMap.end())
  {
    int i = 0;
    while (GAMEDIRECTORY.getFile(QString("Textures\\GuildEmblems\\Emblem_%1_%2_TU_U.blp").arg(icon, 2, 10, QChar('0')).arg(i, 2, 10, QChar('0'))))
      i++;
    maxIconColorMap[icon] = i;
    return i;
  }
	return it->second;
}

int TabardDetails::GetMaxBorder()
{
  if (maxBorder == -1)
  {
    int i = 0;
    while (GAMEDIRECTORY.getFile(QString("Textures\\GuildEmblems\\Border_%1_00_TU_U.blp").arg(i, 2, 10, QChar('0'))))
      i++;
    maxBorder = i;
  }
	return maxBorder;
}

int TabardDetails::GetMaxBorderColor(int border)
{
  auto it = maxBorderColorMap.find(border);

  if (it == maxBorderColorMap.end())
  {
    int i = 0;
    while (GAMEDIRECTORY.getFile(QString("Textures\\GuildEmblems\\Border_%1_%2_TU_U.blp").arg(border, 2, 10, QChar('0')).arg(i, 2, 10, QChar('0'))))
      i++;
    maxBorderColorMap[border] = i;
    return i;
  }

	return it->second;
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
