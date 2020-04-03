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
#include <QEventLoop>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <qjsondocument.h>
#include <QtWidgets/qmessagebox.h>

// Irrlicht

// Externals

// Other libraries
//#include "charcontrol.h"
#include "CharInfos.h"
#include "database.h" // ItemRecord
#include "wow_enums.h"


// Current library


// Namespaces used
//--------------------------------------------------------------------

#define DEBUG_RESULTS 0

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


CharSlots armorySlotToCharSlot(int slot)
{
  if (slot == 0)
    return CS_HEAD;
  else if (slot == 2)
    return CS_SHOULDER;
  else if (slot == 3)
    return CS_SHIRT;
  else if (slot == 4)
    return CS_CHEST;
  else if (slot == 5)
    return CS_BELT;
  else if (slot == 6)
    return CS_PANTS;
  else if (slot == 7)
    return CS_BOOTS;
  else if (slot == 8)
    return CS_BRACERS;
  else if (slot == 9)
    return CS_GLOVES;
  else if (slot == 14)
    return CS_CAPE;
  else if (slot == 15)
    return CS_HAND_RIGHT;
  else if (slot == 16)
    return CS_HAND_LEFT;
  else if (slot == 18)
    return CS_TABARD;
}

CharInfos * ArmoryImporter::importChar(QString url) const
{
	CharInfos * result = new CharInfos();
  QJsonObject root;

	int readStatus = readJSONValues(CHARACTER, url, root);
	// LOG_INFO << "JSON Read Status:" << readStatus << "Root Count:" << root.count();

	if (readStatus == 0 && root.count() > 0)
	{
		LOG_INFO << "Processing JSON Values...";

		// No Gathering Errors Detected.
		result->equipment.resize(NUM_CHAR_SLOTS);
    result->itemModifierIds.resize(NUM_CHAR_SLOTS);

		// Gather Race & Gender
    QJsonObject obj = root.value("playable_race").toObject();
		result->raceId = obj.value("id").toInt();
    obj = root.value("gender").toObject();
    result->gender = obj.value("name").toString().toStdString();

		QJsonObject app = root.value("appearance").toObject();
		result->skinColor = app.value("skin_color").toInt();
		result->faceType = app.value("face_variation").toInt();
		result->hairColor = app.value("hair_color").toInt();
		result->hairStyle = app.value("hair_variation").toInt();
		result->facialHair = app.value("feature_variation").toInt();

#if DEBUG_RESULTS > 0
    LOG_INFO << "result->raceId" << result->raceId;
    LOG_INFO << "result->gender" << result->gender.c_str();
    LOG_INFO << "result->skinColor" << result->skinColor;
    LOG_INFO << "result->faceType" << result->faceType;
    LOG_INFO << "result->hairColor" << result->hairColor;
    LOG_INFO << "result->hairStyle" << result->hairStyle;
    LOG_INFO << "result->facialHair" << result->facialHair;
#endif

		// Gather Demon Hunter options if present
		if (!app.value("custom_display_options").isUndefined() && !app.value("custom_display_options").isNull())
		{
			QJsonArray custom = app.value("custom_display_options").toArray();
			result->isDemonHunter = true;
			result->DHHorns = custom.at(1).toInt();
			result->DHBlindfolds = custom.at(2).toInt();

			int tattoo = custom.at(0).toInt();

			if (tattoo != 0)
			{
				int tattooStyle = tattoo % 6;
				int tattooColor = tattoo / 6;

				if (tattooStyle == 0)
				{
					tattooStyle = 6;
					tattooColor--;
				}

				result->DHTattooStyle = tattooStyle;
				result->DHTattooColor = tattooColor;

#ifdef DEBUG_RESULTS
        LOG_INFO << "Reading demon hunter values";
        LOG_INFO << custom.at(0).toInt() << custom.at(1).toInt() << custom.at(2).toInt();
        LOG_INFO << "result->DHHorns" << result->DHHorns;
        LOG_INFO << "result->DHBlindfolds" << result->DHBlindfolds;
        LOG_INFO << "result->DHTattooStyle" << result->DHTattooStyle;
        LOG_INFO << "result->DHTattooColor" << result->DHTattooColor;
#endif

			}
		}

		// Gather Items
		result->hasTransmogGear = false;
		QJsonArray items = root.value("items").toArray();
		
    for (int i = 0; i < items.size(); ++i)
    {
      CharSlots slot = armorySlotToCharSlot(items.at(i)["internal_slot_id"].toInt());
      result->equipment[slot] = items.at(i)["id"].toInt();
      result->itemModifierIds[slot] = items.at(i)["item_appearance_modifier_id"].toInt();
    }

   
		// Set proper eyeglow
		if (root.value("class").toInt() == 6) // 6 = DEATH KNIGHT
			result->eyeGlowType = EGT_DEATHKNIGHT;
		else
			result->eyeGlowType = EGT_DEFAULT;
		
    
		// tabard (useful if guild tabard)
    QJsonObject guildTabard = root.value("guild_crest").toObject();

    if (!guildTabard.isEmpty())
    {
      result->tabardIcon = guildTabard.value("emblem").toObject().value("id").toInt();
      result->IconColor = guildTabard.value("emblem").toObject().value("color").toObject().value("id").toInt();
      result->tabardBorder = guildTabard.value("border").toObject().value("id").toInt();
      result->BorderColor = guildTabard.value("border").toObject().value("color").toObject().value("id").toInt();
      result->Background = guildTabard.value("background").toObject().value("color").toObject().value("id").toInt();
     
      result->customTabard = true;
    }

		result->valid = true;
	}
	else {
		LOG_ERROR << "Bad JSON Results:" << readStatus << "Root Size:" << root.count();
	}
	
	return result;
}

ItemRecord * ArmoryImporter::importItem(QString url) const
{
	QJsonObject root;
	ItemRecord * result = NULL;

	if (readJSONValues(ITEM, url, root) == 0 && root.count() != 0)
	{
		// No Gathering Errors Detected.
		result = new ItemRecord();

		// Gather Race & Gender
		result->id = root.value("id").toInt();
		result->model = root.value("displayInfoId").toInt();
		result->name = root.value("name").toString().toUtf8();
		result->itemclass = root.value("itemClass").toInt();
		result->subclass = root.value("itemSubClass").toInt();
		result->quality = root.value("quality").toInt();
		result->type = root.value("inventoryType").toInt();
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

					// Using a QMessageBox can easily fix this:
					// QMessageBox msg(QMessageBox::Icon::Critical, tr("Bad Armory Link"), tr("Improperly Formatted URL.\n\nMake sure your link ends in /simple or /advanced."), QMessageBox::StandardButton::Ok);
					// msg.exec();
					LOG_ERROR << "Improperly Formatted URL. Lacks /simple and /advanced";
					return 2;
				}

				QStringList strList = strURL.mid(7).split("/");

				region = strList.at(0).mid(0, strList.at(0).indexOf("."));
				realm = strList.at(strList.size() - 3);
				charName = strList.at(strList.size() - 2).mid(0, strURL.lastIndexOf("?") - 1);
				LOG_INFO << "Battle Net, CharName: " << charName << " Realm: " << realm << " Region: " << region;
			}
			else if (strURL.indexOf("worldofwarcraft.com") != -1)
			{
				// Import from https://worldofwarcraft.com/fr-fr/character/les-sentinelles/jeromnimo
        // or (new form) https://worldofwarcraft.com/fr-fr/character/eu/les-sentinelles/jeromnimo

				LOG_INFO << qPrintable(strURL);
				QStringList strList = strURL.mid(8).split("/");


				region = strList.at(1);
				realm = strList.at(strList.size() - 2);
				charName = strList.at(strList.size() - 1).mid(0, strURL.lastIndexOf("?") - 1);
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
				// QMessageBox msg(QMessageBox::Icon::Critical, tr("Bad Armory Link"), tr("Improperly Formatted URL.\n\nThe domain should be worldofwarcraft.com"), QMessageBox::StandardButton::Ok);
				// msg.exec();
				LOG_ERROR << "Improperly Formatted URL. The domain should be worldofwarcraft.com";
				return 2;
			}

			LOG_INFO << "Loading Battle.Net Armory. Region:" << qPrintable(region)
				<< ", Realm:" << qPrintable(realm)
				<< ", Character:" << qPrintable(charName);

			apiPage = QString("https://wowmodelviewer.net/armory2.php?region=%1&realm=%2&char=%3").arg(region).arg(realm).arg(charName);
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

	LOG_INFO << "Final API Page:" << qPrintable(apiPage);

	QByteArray bts = getURLData(apiPage);
  LOG_INFO << bts;
	result = QJsonDocument::fromJson(bts).object();
	return 0;
}

QByteArray ArmoryImporter::getURLData(QString inputUrl) const
{
	QUrl url = QString(inputUrl);
	// LOG_INFO << "Getting info from:" << qPrintable(url.toString());

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

	//LOG_INFO << "Returning Response:" << qPrintable(htmldata);

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