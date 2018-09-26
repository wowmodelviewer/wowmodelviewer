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
 * ArmoryImporter.cpp
 *
 *  Created on: 9 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _ARMORYIMPORTER_CPP_
#include "ArmoryImporter.h"
#undef _ARMORYIMPORTER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QUrl>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <qjsondocument.h>

// Irrlicht

// Externals

// Other libraries
//#include "charcontrol.h"
#include "CharInfos.h"
#include "TabardDetails.h"
#include "database.h" // ItemRecord
#include "globalvars.h"
#include "wow_enums.h"


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
bool ArmoryImporter::acceptURL(QString url) const
{
  return ((url.indexOf("battle.net") != -1) || (url.indexOf("worldofwarcraft.com") != -1));
}


CharInfos * ArmoryImporter::importChar(QString url) const
{
	CharInfos * result = new CharInfos();
	QJsonObject root;

	int readStatus = readJSONValues(CHARACTER, url, root);

	if (readStatus == 0 && root.size() > 0)
	{
		// No Gathering Errors Detected.
		result->equipment.resize(NUM_CHAR_SLOTS);

		// Gather Race & Gender
		result->raceId = root["race"].toInt();
		result->gender = (root["gender"].toInt() == 0) ? "Male" : "Female";

		QJsonObject app = root["appearance"].toObject();
		result->skinColor = app["skinColor"].toInt();
		result->faceType = app["faceVariation"].toInt();
		result->hairColor = app["hairColor"].toInt();
		result->hairStyle = app["hairVariation"].toInt();
		result->facialHair = app["featureVariation"].toInt();

		// Gather Demon Hunter options if present
		if (!app["customDisplayOptions"].isUndefined() && !app["customDisplayOptions"].isNull())
		{
			QJsonArray custom = app["customDisplayOptions"].toArray();
			result->isDemonHunter = true;
			result->DHHorns = custom.at(1).toInt();
			result->DHBlindfolds = custom.at(2).toInt();

			int tatoo = custom.at(0).toInt();

			if (tatoo != 0)
			{
				int tatooStyle = tatoo % 6;
				int tatooColor = tatoo / 6;

				if (tatooStyle == 0)
				{
					tatooStyle = 6;
					tatooColor--;
				}

				result->DHTatooStyle = tatooStyle;
				result->DHTatooColor = tatooColor;
			}
		}

		// Gather Items
		result->hasTransmogGear = false;
		QJsonObject items = root["items"].toObject();
		
		if (!items["back"].isUndefined() && !items["back"].isNull())
		{
			result->equipment[CS_CAPE] = items["back"].toObject()["id"].toInt();
			if (hasTransmog(items["back"]))
			{
				result->equipment[CS_CAPE] = items["back"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["chest"].isUndefined() && !items["chest"].isNull())
		{
			result->equipment[CS_CHEST] = items["chest"].toObject()["id"].toInt();
			if (hasTransmog(items["chest"]))
			{
				result->equipment[CS_CHEST] = items["chest"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["feet"].isUndefined() && !items["feet"].isNull())
		{
			result->equipment[CS_BOOTS] = items["feet"].toObject()["id"].toInt();
			if (hasTransmog(items["feet"]))
			{
				result->equipment[CS_BOOTS] = items["feet"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["hands"].isUndefined() && !items["hands"].isNull())
		{
			result->equipment[CS_GLOVES] = items["hands"].toObject()["id"].toInt();
			if (hasTransmog(items["hands"]))
			{
				result->equipment[CS_GLOVES] = items["hands"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["head"].isUndefined() && !items["head"].isNull())
		{
			result->equipment[CS_HEAD] = items["head"].toObject()["id"].toInt();
			if (hasTransmog(items["head"]))
			{
				result->equipment[CS_HEAD] = items["head"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["legs"].isUndefined() && !items["legs"].isNull())
		{
			result->equipment[CS_PANTS] = items["legs"].toObject()["id"].toInt();
			if (hasTransmog(items["legs"]))
			{
				result->equipment[CS_PANTS] = items["legs"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["mainHand"].isUndefined() && !items["mainHand"].isNull())
		{
			result->equipment[CS_HAND_RIGHT] = items["mainHand"].toObject()["id"].toInt();
			if (hasTransmog(items["mainHand"]))
			{
				result->equipment[CS_HAND_RIGHT] = items["mainHand"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["offHand"].isUndefined() && !items["offHand"].isNull())
		{
			result->equipment[CS_HAND_LEFT] = items["offHand"].toObject()["id"].toInt();
			if (hasTransmog(items["offHand"]))
			{
				result->equipment[CS_HAND_LEFT] = items["offHand"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["shirt"].isUndefined() && !items["shirt"].isNull())
		{
			result->equipment[CS_SHIRT] = items["shirt"].toObject()["id"].toInt();
			if (hasTransmog(items["shirt"]))
			{
				result->equipment[CS_SHIRT] = items["shirt"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["shoulder"].isUndefined() && !items["shoulder"].isNull())
		{
			result->equipment[CS_SHOULDER] = items["shoulder"].toObject()["id"].toInt();
			if (hasTransmog(items["shoulder"]))
			{
				result->equipment[CS_SHOULDER] = items["shoulder"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["tabard"].isUndefined() && !items["tabard"].isNull())
		{
			result->equipment[CS_TABARD] = items["tabard"].toObject()["id"].toInt();
			if (hasTransmog(items["tabard"]))
			{
				result->equipment[CS_TABARD] = items["tabard"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["waist"].isUndefined() && !items["waist"].isNull())
		{
			result->equipment[CS_BELT] = items["waist"].toObject()["id"].toInt();
			if (hasTransmog(items["waist"]))
			{
				result->equipment[CS_BELT] = items["waist"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		if (!items["wrist"].isUndefined() && !items["wrist"].isNull())
		{
			result->equipment[CS_BRACERS] = items["wrist"].toObject()["id"].toInt();
			if (hasTransmog(items["wrist"]))
			{
				result->equipment[CS_BRACERS] = items["wrist"].toObject()["tooltipParams"].toObject()["transmogItem"].toInt();
				result->hasTransmogGear = true;
			}
		}
		
		// Set proper eyeglow
		if (root["class"].toInt() == 6) // 6 = DEATH KNIGHT
			result->eyeGlowType = EGT_DEATHKNIGHT;
		else
			result->eyeGlowType = EGT_DEFAULT;
		
		// tabard (useful if guild tabard)
		QJsonObject guild = root["guild"].toObject();
		if(guild.size() > 0)
		{
			QJsonObject tabard = guild["emblem"].toObject();
			if(tabard.size() > 0)
			{
				result->tabardIcon = tabard["icon"].toInt();
				result->tabardBorder = tabard["border"].toInt();
				result->BorderColor = tabard["borderColorId"].toInt();
				result->Background = tabard["backgroundColorId"].toInt();
				result->IconColor = tabard["iconColorId"].toInt();
				result->customTabard = true;
			}
		}
		result->valid = true;
	}
	
	return result;
}

ItemRecord * ArmoryImporter::importItem(QString url) const
{
	QJsonObject root;
	ItemRecord * result = NULL;

	if (readJSONValues(ITEM, url, root) == 0 && root.size() != 0)
	{
		// No Gathering Errors Detected.
		result = new ItemRecord();

		// Gather Race & Gender
		result->id = root["id"].toInt();
		result->model = root["displayInfoId"].toInt();
		result->name = root["name"].toString().toUtf8();
		result->itemclass = root["itemClass"].toInt();
		result->subclass = root["itemSubClass"].toInt();
		result->quality = root["quality"].toInt();
		result->type = root["inventoryType"].toInt();
	}

	return result;
}


// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
int ArmoryImporter::readJSONValues(ImportType type, QString url, QJsonObject & result) const
{
	QString apiPage;
	switch (type)
	{
		case CHARACTER:
		{
			/*
				Blizzard's API is mostly RESTful, with data being returned as JSON arrays.
				Full documentation available here: http://blizzard.github.com/api-wow-docs/

				We can now gather all the data with a single request of Host + "/api/wow/character/" + Realm + "/" + CharacterName + "?fields=appearance,items"
				Example: http://us.battle.net/api/wow/character/steamwheedle-cartel/Kjasi?fields=appearance,items

				This will give us all the information we need inside of a JSON array.
				Format as follows:
				{
					"lastModified":1319438058000
					"name":"Kjasi",
					"realm":"Steamwheedle Cartel",
					"class":5,
					"race":1,
					"gender":0,
					"level":83,
					"achievementPoints":4290,
					"thumbnail":"steamwheedle-cartel/193/3589057-avatar.jpg",
					"items":{	This is the Items array. All available item information is listed here.
						"averageItemLevel":298,
						"averageItemLevelEquipped":277,
						"head":{
							"id":50006,
							"name":"Corp'rethar Ceremonial Crown",
							"icon":"inv_helmet_156",
							"quality":4,
							"tooltipParams":{
								"gem0":41376,
								"gem1":40151,
								"enchant":3819,
								"reforge":119
								"transmogItem":63672
							}
						},
						(More slots),
						"ranged":{
							"id":55480,
							"name":"Swamplight Wand of the Invoker",
							"icon":"inv_wand_1h_cataclysm_b_01",
							"quality":2,
							"tooltipParams":{
								"suffix":-39
							}
						}
					},
					"appearance":{
						"faceVariation":11,
						"skinColor":1,
						"hairVariation":11,
						"hairColor":4,
						"featureVariation":1,
						"showHelm":true,
						"showCloak":true,

					"customDisplayOptions":[
							33,
							0,
							1
						]
					}
				}

				As you can see, this will give us almost all the data we need to properly rebuild the character.

			*/
			QString strURL(url);

			QString region = QString();
			QString realm = QString();
			QString charName = QString();

			// Seems to redirect to worldofwarcraft.com as of Sept 2018.
			if (strURL.indexOf("battle.net") != -1)
			{
				// Import from http://us.battle.net/wow/en/character/steamwheedle-cartel/Kjasi/simple

				if ((strURL.indexOf("simple") == -1) &&
					(strURL.indexOf("advanced") == -1))
				{
					// due to Qt plugin, this cause application crash
					// temporary solution : cascade return value to main app to display the pop up (see modelviewer.cpp)
					//wxMessageBox(tr("Improperly Formatted URL.\nMake sure your link ends in /simple or /advanced."),tr("Bad Armory Link"));
					LOG_ERROR << "Improperly Formatted URL. Lacks /simple and /advanced";
					return 2;
				}

				QStringList strList = strURL.mid(7).split("/");

				region = strList.at(0).mid(0, strURL.indexOf("."));
				realm = strList.at(4);
				charName = strList.at(5).mid(0, strURL.lastIndexOf("?") - 1);
				LOG_INFO << "Battle Net, CharName: " << charName << " Realm: " << realm << " Region: " << region;
			}
			else if (strURL.indexOf("worldofwarcraft.com") != -1)
			{
				// Import from https://worldofwarcraft.com/fr-fr/character/les-sentinelles/jeromnimo

				LOG_INFO << qPrintable(strURL);
				QStringList strList = strURL.mid(8).split("/");


				region = strList.at(1);
				realm = strList.at(3);
				charName = strList.at(4).mid(0, strURL.lastIndexOf("?") - 1);
				LOG_INFO << "WoW.com, CharName:" << charName << "Realm:" << realm << "Region:" << region;
				
				// I don't believe these should be translated, as websites tend not to translate URLs...
				// If so, change to region == tr("fr-fr")
				if ((region == "fr-fr") || (region == "en-gb"))
					region = "eu";
				else if (region == "en-us")
					region = "us";
				else if (region == "zh-tw")
					region = "tw";
				else if (region == "ko-kr")
					region = "kr";
			}
			else
			{
				LOG_ERROR << "Improperly Formatted URL. Should be on domain worldofwarcraft.com";
				return 2;
			}

			LOG_INFO << "Loading Battle.Net Armory. Region:" << qPrintable(region)
				<< ", Realm:" << qPrintable(realm)
				<< ", Character:" << qPrintable(charName);

			apiPage = QString("https://wowmodelviewer.net/armory.php?region=%1&realm=%2&char=%3").arg(region).arg(realm).arg(charName);
			break;
		}
		case ITEM:
		{
			// url given is something like http://eu.battle.net/wow/fr/item/104673
			// we need :
			// 1. base battle.net address
			// 2. locale (fr in above example) - Later
			// 3. item number

			// for the sake of simplicity, only handle english name for now

			QString strURL(url);
			QString itemNumber = strURL.mid(strURL.lastIndexOf("/"));

			LOG_INFO << "Loading Battle.Net Armory. Item: " << qPrintable(itemNumber);

			apiPage = QString("https://wowmodelviewer.net/armory.php?item=%1").arg(itemNumber);

			break;
		}
		default:
			LOG_ERROR << "Invalid Import Type: " << type;
			return 3;
			break;
	}

	LOG_INFO << "Final API Page: " << qPrintable(apiPage);

	QByteArray bts = getURLData(apiPage);

	QJsonDocument doc = QJsonDocument::fromBinaryData(bts);
	result = doc.object();
	return 0;
}

QByteArray ArmoryImporter::getURLData(QString inputUrl) const
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

bool ArmoryImporter::hasMember(QJsonValueRef check, QString lookfor) const
{
	if (check.toObject().find(lookfor) != check.toObject().end())
		return true;
	return false;
}

bool ArmoryImporter::hasTransmog(QJsonValueRef check) const
{
	return hasMember(check.toObject()["tooltipParams"], "transmogItem");
}