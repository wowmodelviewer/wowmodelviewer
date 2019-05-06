/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * WowheadImporter.cpp
 *
 *  Created on: 1 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _WOWHEADIMPORTER_CPP_
#include "WowheadImporter.h"
#undef _WOWHEADIMPORTER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

// Irrlicht

// Externals

// Other libraries
#include "database.h" // ItemRecord
#include "NPCInfos.h"

// Current library


// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------


// Constructors
//--------------------------------------------------------------------


// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
bool WowheadImporter::acceptURL(QString url) const
{
  return (url.indexOf("wowhead") != -1);
}

NPCInfos * WowheadImporter::importNPC(QString urlToGrab) const
{
	NPCInfos * result = NULL;

	// Get the HTML...
	QString htmldata = QString(getURLData(urlToGrab)).toUtf8();
	if (htmldata.isNull() || htmldata.isEmpty())
		return NULL;

	// let's go : finding name
	// extract global infos
	QString infos = extractSubString(htmldata, "(g_npcs[", ";");

	// finding name
	QString NPCName = extractSubString(infos, "name\":\"", "\",");

	// finding type
	int NPCType = extractSubString(infos, "type\":", "}").toInt();

	// finding id
	int NPCId = extractSubString(infos, "id\":", ",").toInt();

	// display id
  QString NPCDispIdstr = extractSubString(htmldata, "ModelViewer.show({");
  NPCDispIdstr = extractSubString(NPCDispIdstr, "displayId: ", "}");

	if (NPCDispIdstr.indexOf(",") != -1) // comma at end of id
		NPCDispIdstr = NPCDispIdstr.mid(0, NPCDispIdstr.indexOf(","));

	int NPCDispId = NPCDispIdstr.toInt();

	result = new NPCInfos();

	result->name = NPCName.toStdWString();
	result->type = NPCType;
	result->id = NPCId;
	result->displayId = NPCDispId;

	return result;
}

ItemRecord * WowheadImporter::importItem(QString urlToGrab) const
{
	ItemRecord * result = NULL;

	// Get the HTML...
	QString htmldata = QString(getURLData(urlToGrab)).toUtf8();
	if (htmldata.isNull() || htmldata.isEmpty())
		return NULL;

	// let's go : finding name
	// extract global infos
  QString data = extractSubString(htmldata, "(g_items[", ";");
  data = extractSubString(data, "],");
  data.chop(1);
  
  QJsonParseError error;
  QJsonObject infos = QJsonDocument::fromJson(data.toUtf8(), &error).object();

  LOG_INFO << error.errorString();

  if (infos.count() != 0)
  {
    result = new ItemRecord();

    result->name = infos.value("name").toString().toUtf8();
    result->type = infos.value("slot").toInt();
    result->id = infos.value("id").toInt();
    result->model = infos.value("displayid").toInt();  
    result->itemclass = infos.value("classs").toInt();
    result->subclass = infos.value("subclass").toInt();
  }

	return result;
}


// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
QString WowheadImporter::extractSubString(QString & datas, QString beginPattern, QString endPattern) const
{
	QString result;
	try
	{
    result = datas.mid(datas.indexOf(beginPattern, 0, Qt::CaseInsensitive)+beginPattern.length());
    if (!endPattern.isEmpty())
      result = result.mid(0, result.indexOf(endPattern, 0, Qt::CaseInsensitive));
	}
	catch (...)
	{
	}
	return result;
}

QByteArray WowheadImporter::getURLData(QString inputUrl) const
{
	QUrl url = QString(inputUrl);

	if (!url.errorString().isEmpty())
	{
		return QByteArray();
	}

	QNetworkAccessManager manager;
	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", "WoWModelViewer");
	QNetworkReply *response = manager.get(request);
	QEventLoop eventLoop;
	connect(response, SIGNAL(finished()), &eventLoop, SLOT(quit()));
	connect(response, SIGNAL(error(QNetworkReply::NetworkError)), &eventLoop, SLOT(quit()));
	eventLoop.exec();

	QByteArray htmldata = response->readAll(); // Source should be stored here

	return htmldata;
}