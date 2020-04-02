/*
 * TabardDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "TabardDetails.h"

#include "Game.h"
#include "wow_enums.h"

#include "logger/Logger.h"

#include <QXmlStreamWriter>

TabardDetails::TabardDetails()
{
  QString query = QString("SELECT DISTINCT Color FROM GuildTabardBackground");
  sqlResult r = GAMEDATABASE.sqlQuery(query);

  if (r.valid && !r.values.empty())
    for (auto& v : r.values)
      backgrounds.push_back(v[0].toInt());

  query = QString("SELECT DISTINCT EmblemID FROM GuildTabardEmblem");
  r = GAMEDATABASE.sqlQuery(query);

  if (r.valid && !r.values.empty())
    for (auto& v : r.values)
      icons.push_back(v[0].toInt());

  query = QString("SELECT DISTINCT BorderID FROM GuildTabardBorder");
  r = GAMEDATABASE.sqlQuery(query);

  if (r.valid && !r.values.empty())
    for (auto& v : r.values)
      borders.push_back(v[0].toInt());
}

GameFile * TabardDetails::GetBackgroundTex(int slot)
{
  GameFile * result = nullptr;
  QString query = QString("SELECT FileDataID FROM GuildTabardBackground WHERE Color=%1 AND Tier=%2 AND Component=%3")
                         .arg(backgrounds[Background]).arg(0).arg(slot);

  sqlResult r = GAMEDATABASE.sqlQuery(query);

  if (r.valid && !r.values.empty())
    result = GAMEDIRECTORY.getFile(r.values[0][0].toInt());

  return result;
}

GameFile * TabardDetails::GetBorderTex(int slot)
{
  GameFile * result = nullptr;
  QString query = QString("SELECT FileDataID FROM GuildTabardBorder WHERE BorderID=%1 AND Color=%2 AND Tier=%3 AND Component=%4")
    .arg(borders[Border]).arg(BorderColor).arg(0).arg(slot);

  sqlResult r = GAMEDATABASE.sqlQuery(query);

  if (r.valid && !r.values.empty())
    result = GAMEDIRECTORY.getFile(r.values[0][0].toInt());

  return result;
}

GameFile * TabardDetails::GetIconTex(int slot)
{
  GameFile * result = nullptr;
  QString query = QString("SELECT FileDataID FROM GuildTabardEmblem WHERE EmblemID=%1 AND Color=%2 AND Component=%3")
    .arg(icons[Icon]).arg(IconColor).arg(slot);

  sqlResult r = GAMEDATABASE.sqlQuery(query);

  if (r.valid && !r.values.empty())
    result = GAMEDIRECTORY.getFile(r.values[0][0].toInt());

  return result;
}

int TabardDetails::GetMaxBackground()
{
  return backgrounds.size();
}

int TabardDetails::GetMaxIcon()
{
  return icons.size();
}

int TabardDetails::GetMaxIconColor(int icon)
{
  QString query = QString("SELECT COUNT(*) FROM(SELECT DISTINCT Color FROM GuildTabardEmblem WHERE EmblemID = %1)").arg(icon);

  sqlResult r = GAMEDATABASE.sqlQuery(query);

  if (r.valid && !r.values.empty())
    return r.values[0][0].toInt();

  return -1;
}

int TabardDetails::GetMaxBorder()
{
  return borders.size();
}

int TabardDetails::GetMaxBorderColor(int border)
{
  QString query = QString("SELECT COUNT(*) FROM (SELECT DISTINCT Color FROM GuildTabardBorder WHERE BorderID = 5)").arg(border);

  sqlResult r = GAMEDATABASE.sqlQuery(query);

  if (r.valid && !r.values.empty())
    return r.values[0][0].toInt();

  return -1;
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

int TabardDetails::getIcon()
{
  return Icon;
}

int TabardDetails::getIconColor()
{
  return IconColor;
}

int TabardDetails::getBorder()
{
  return Border;
}

int TabardDetails::getBorderColor()
{
  return BorderColor;
}

int TabardDetails::getBackground()
{
  return Background;
}

void TabardDetails::setIcon(int icon)
{
  Icon = icon;
}

void TabardDetails::setIconColor(int color)
{
  IconColor = color;
}

void TabardDetails::setBorder(int border)
{
  border = border;
}

void TabardDetails::setBorderColor(int color)
{
  BorderColor = color;
}

void TabardDetails::setBackground(int background)
{
  Background = background;
}


