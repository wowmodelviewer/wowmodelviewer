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

#include "ArmoryImporter.h"

#include <QEventLoop>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <qjsondocument.h>
#include <QtWidgets/qmessagebox.h>

//#include "charcontrol.h"
#include "CharInfos.h"
#include "database.h" // ItemRecord
#include "wow_enums.h"

#define DEBUG_RESULTS 0

bool ArmoryImporter::acceptURL(QString url) const
{
	return ((url.indexOf("battle.net") != -1) || (url.indexOf("worldofwarcraft.com") != -1) || (url.
		indexOf("blizzard.com") != -1));
}

CharSlots armorySlotToCharSlot(const int slot)
{
	if (slot == 0)
		return CS_HEAD;
	if (slot == 2)
		return CS_SHOULDER;
	if (slot == 3)
		return CS_SHIRT;
	if (slot == 4)
		return CS_CHEST;
	if (slot == 5)
		return CS_BELT;
	if (slot == 6)
		return CS_PANTS;
	if (slot == 7)
		return CS_BOOTS;
	if (slot == 8)
		return CS_BRACERS;
	if (slot == 9)
		return CS_GLOVES;
	if (slot == 14)
		return CS_CAPE;
	if (slot == 15)
		return CS_HAND_RIGHT;
	if (slot == 16)
		return CS_HAND_LEFT;
	if (slot == 18)
		return CS_TABARD;

	return {};
}

CharInfos* ArmoryImporter::importChar(QString url) const
{
	auto* result = new CharInfos();
	QJsonObject root;

	const auto readStatus = readJSONValues(CHARACTER, url, root);
	// LOG_INFO << "JSON Read Status:" << readStatus << "Root Count:" << root.count();

	if (readStatus == 0 && root.count() > 0)
	{
		LOG_INFO << "Processing JSON Values...";

		// No Gathering Errors Detected.
		result->equipment.resize(NUM_CHAR_SLOTS);
		result->itemModifierIds.resize(NUM_CHAR_SLOTS);

		// Gather Race & Gender
		auto obj = root.value("playable_race").toObject();
		result->raceId = obj.value("id").toInt();
		obj = root.value("gender").toObject();
		result->gender = obj.value("name").toString().toStdString();

		// Gather character customizations
		const auto customizations = root.value("customizations").toArray();
		for (const auto& customization : customizations)
		{
			auto optionid = customization.toObject().value("option").toObject().value("id").toInt();
			auto choiceid = customization.toObject().value("choice").toObject().value("id").toInt();
			result->customizations.emplace_back(optionid, choiceid);
		}


		// Gather Items
		result->hasTransmogGear = false;
		const auto Items = root.value("items").toArray();

		for (const auto& item : Items)
		{
			const auto slot = armorySlotToCharSlot(item["internal_slot_id"].toInt());
			result->equipment[slot] = item["id"].toInt();
			result->itemModifierIds[slot] = item["item_appearance_modifier_id"].toInt();
		}


		// Set proper eyeglow
		if (root.value("class").toInt() == 6) // 6 = DEATH KNIGHT
			result->eyeGlowType = EGT_DEATHKNIGHT;
		else
			result->eyeGlowType = EGT_DEFAULT;


		// tabard (useful if guild tabard)
		const auto guildTabard = root.value("guild_crest").toObject();

		if (!guildTabard.isEmpty())
		{
			result->tabardIcon = guildTabard.value("emblem").toObject().value("id").toInt();
			result->iconColor = guildTabard.value("emblem").toObject().value("color").toObject().value("id").toInt();
			result->tabardBorder = guildTabard.value("border").toObject().value("id").toInt();
			result->borderColor = guildTabard.value("border").toObject().value("color").toObject().value("id").toInt();
			result->background = guildTabard.value("background").toObject().value("color").toObject().value("id").
			                                 toInt();

			result->customTabard = true;
		}

		result->valid = true;
	}
	else
	{
		LOG_ERROR << "Bad JSON Results:" << readStatus << "Root Size:" << root.count();
	}

	return result;
}

ItemRecord* ArmoryImporter::importItem(QString url) const
{
	QJsonObject root;
	ItemRecord* result = nullptr;

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

int ArmoryImporter::readJSONValues(ImportType type, const QString& url, QJsonObject& result) const
{
	QString apiPage;
	switch (type)
	{
	case CHARACTER:
		{
			/*
			blizzard's API is mostly RESTful, with data being returned as JSON arrays.
			Full documentation available here: http://blizzard.github.com/api-wow-docs/
			
			Example: https://eu.api.blizzard.com/profile/wow/character/les-sentinelles/jeromnimo/appearance?namespace=profile-eu&locale=fr_FR
			
			This will give us all the information we need inside of a JSON array.
			{
			  "_links": {
			    "self": {
			      "href": "https://eu.api.blizzard.com/profile/wow/character/les-sentinelles/jeromnimo/appearance?namespace=profile-eu"
			    }
			  },
			  "character": {
			    "key": {
			      "href": "https://eu.api.blizzard.com/profile/wow/character/les-sentinelles/jeromnimo?namespace=profile-eu"
			    },
			    "name": "Jeromnimo",
			    "id": 82483610,
			    "realm": {
			      "key": {
			        "href": "https://eu.api.blizzard.com/data/wow/realm/647?namespace=dynamic-eu"
			      },
			      "name": "Les Sentinelles",
			      "id": 647,
			      "slug": "les-sentinelles"
			    }
			  },
			  "playable_race": {
			    "key": {
			      "href": "https://eu.api.blizzard.com/data/wow/playable-race/5?namespace=static-9.0.1_36072-eu"
			    },
			    "name": "Mort-vivant",
			    "id": 5
			  },
			  "playable_class": {
			    "key": {
			      "href": "https://eu.api.blizzard.com/data/wow/playable-class/9?namespace=static-9.0.1_36072-eu"
			    },
			    "name": "Démoniste",
			    "id": 9
			  },
			  "active_spec": {
			    "key": {
			      "href": "https://eu.api.blizzard.com/data/wow/playable-specialization/265?namespace=static-9.0.1_36072-eu"
			    },
			    "name": "Affliction",
			    "id": 265
			  },
			  "gender": {
			    "type": "MALE",
			    "name": "Homme"
			  },
			  "faction": {
			    "type": "HORDE",
			    "name": "Horde"
			  },
			  "guild_crest": {
			    "emblem": {
			      "id": 126,
			      "media": {
			        "key": {
			          "href": "https://eu.api.blizzard.com/data/wow/media/guild-crest/emblem/126?namespace=static-9.0.1_36072-eu"
			        },
			        "id": 126
			      },
			      "color": {
			        "id": 14,
			        "rgba": {
			          "r": 177,
			          "g": 184,
			          "b": 177,
			          "a": 1
			        }
			      }
			    },
			    "border": {
			      "id": 0,
			      "media": {
			        "key": {
			          "href": "https://eu.api.blizzard.com/data/wow/media/guild-crest/border/0?namespace=static-9.0.1_36072-eu"
			        },
			        "id": 0
			      },
			      "color": {
			        "id": 0,
			        "rgba": {
			          "r": 103,
			          "g": 0,
			          "b": 33,
			          "a": 1
			        }
			      }
			    },
			    "background": {
			      "color": {
			        "id": 44,
			        "rgba": {
			          "r": 79,
			          "g": 35,
			          "b": 0,
			          "a": 1
			        }
			      }
			    }
			  },
			  "items": [
			    {
			      "id": 132394,
			      "slot": {
			        "type": "HEAD",
			        "name": "Tête"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 0,
			      "internal_slot_id": 0,
			      "subclass": 1
			    },
			    {
			      "id": 134309,
			      "slot": {
			        "type": "SHOULDER",
			        "name": "Épaules"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 0,
			      "internal_slot_id": 2,
			      "subclass": 1
			    },
			    {
			      "id": 4333,
			      "slot": {
			        "type": "SHIRT",
			        "name": "Chemise"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 0,
			      "internal_slot_id": 3,
			      "subclass": 0
			    },
			    {
			      "id": 134307,
			      "slot": {
			        "type": "CHEST",
			        "name": "Torse"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 0,
			      "internal_slot_id": 4,
			      "subclass": 1
			    },
			    {
			      "id": 134303,
			      "slot": {
			        "type": "WAIST",
			        "name": "Taille"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 0,
			      "internal_slot_id": 5,
			      "subclass": 1
			    },
			    {
			      "id": 134306,
			      "slot": {
			        "type": "LEGS",
			        "name": "Jambes"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 0,
			      "internal_slot_id": 6,
			      "subclass": 1
			    },
			    {
			      "id": 134417,
			      "slot": {
			        "type": "FEET",
			        "name": "Pieds"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 3,
			      "internal_slot_id": 7,
			      "subclass": 1
			    },
			    {
			      "id": 134178,
			      "slot": {
			        "type": "WRIST",
			        "name": "Poignets"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 0,
			      "internal_slot_id": 8,
			      "subclass": 1
			    },
			    {
			      "id": 133609,
			      "slot": {
			        "type": "HANDS",
			        "name": "Mains"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 3,
			      "internal_slot_id": 9,
			      "subclass": 1
			    },
			    {
			      "id": 134402,
			      "slot": {
			        "type": "BACK",
			        "name": "Dos"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 3,
			      "internal_slot_id": 14,
			      "subclass": 1
			    },
			    {
			      "id": 128942,
			      "slot": {
			        "type": "MAIN_HAND",
			        "name": "Main droite"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 24,
			      "internal_slot_id": 15,
			      "subclass": 10
			    },
			    {
			      "id": 140578,
			      "slot": {
			        "type": "TABARD",
			        "name": "Tabard"
			      },
			      "enchant": 0,
			      "item_appearance_modifier_id": 0,
			      "internal_slot_id": 18,
			      "subclass": 0
			    }
			  ],
			  "customizations": [
			    {
			      "option": {
			        "name": "Peau",
			        "id": 58
			      },
			      "choice": {
			        "id": 916,
			        "display_order": 3
			      }
			    },
			    {
			      "option": {
			        "name": "Visage",
			        "id": 59
			      },
			      "choice": {
			        "id": 924,
			        "display_order": 4
			      }
			    },
			    {
			      "option": {
			        "name": "Coiffure",
			        "id": 60
			      },
			      "choice": {
			        "name": "Iroquoise",
			        "id": 941,
			        "display_order": 1
			      }
			    },
			    {
			      "option": {
			        "name": "Couleur des cheveux",
			        "id": 61
			      },
			      "choice": {
			        "id": 961,
			        "display_order": 5
			      }
			    },
			    {
			      "option": {
			        "name": "Détails de la mâchoire",
			        "id": 62
			      },
			      "choice": {
			        "name": "Joues nécrosées",
			        "id": 979,
			        "display_order": 9
			      }
			    },
			    {
			      "option": {
			        "name": "Couleur des yeux",
			        "id": 534
			      },
			      "choice": {
			        "id": 5330,
			        "display_order": 0
			      }
			    },
			    {
			      "option": {
			        "name": "Détails du visage",
			        "id": 563
			      },
			      "choice": {
			        "name": "Aucun",
			        "id": 6287,
			        "display_order": 0
			      }
			    },
			    {
			      "option": {
			        "name": "Type de peau",
			        "id": 567
			      },
			      "choice": {
			        "name": "Os apparents",
			        "id": 6527,
			        "display_order": 0
			      }
			    }
			  ]
			}
			
			As you can see, this will give us almost all the data we need to properly rebuild the character.
			
			*/

			const auto& strUrl(url);

			QString region;
			QString realm;
			QString charName;

			// Seems to redirect to worldofwarcraft.com as of Sept 2018.
			if (strUrl.indexOf("battle.net") != -1)
			{
				// Import from http://us.battle.net/wow/en/character/steamwheedle-cartel/Kjasi/simple

				if ((strUrl.indexOf("simple") == -1) &&
					(strUrl.indexOf("advanced") == -1))
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

				const auto strList = strUrl.mid(7).split("/");

				region = strList.at(0).mid(0, strList.at(0).indexOf("."));
				realm = strList.at(strList.size() - 3);
				charName = strList.at(strList.size() - 2).mid(0, strUrl.lastIndexOf("?") - 1);
				LOG_INFO << "Battle Net, CharName: " << charName << " Realm: " << realm << " Region: " << region;
			}
			else if ((strUrl.indexOf("worldofwarcraft.com") != -1) || (url.indexOf("blizzard.com") != -1))
			{
				// Import from https://worldofwarcraft.com/fr-fr/character/les-sentinelles/jeromnimo
				// or (new form) https://worldofwarcraft.com/fr-fr/character/eu/les-sentinelles/jeromnimo
				// or (new in 2023) https://worldofwarcraft.blizzard.com/en-gb/character/eu/silvermoon/n%C3%A1tnat

				LOG_INFO << qPrintable(strUrl);
				const auto strList = strUrl.mid(8).split("/");

				if (strList.size() > 5) // new form
					region = strList.at(3);
				else
					region = strList.at(1);

				realm = strList.at(strList.size() - 2);
				charName = strList.at(strList.size() - 1).mid(0, strUrl.lastIndexOf("?") - 1);
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
				LOG_ERROR << "Improperly Formatted URL. The domain should be worldofwarcraft.com or blizzard.com";
				return 2;
			}

			LOG_INFO << "Loading Battle.Net Armory. Region:" << qPrintable(region)
				<< ", Realm:" << qPrintable(realm)
				<< ", Character:" << qPrintable(charName);

			apiPage = QString("https://wowmodelviewer.net/armory2.php?region=%1&realm=%2&char=%3").arg(region).
				arg(realm).arg(charName);
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

			const auto& strUrl(url);
			const auto itemNumber = strUrl.mid(strUrl.lastIndexOf("/"));

			LOG_INFO << "Loading Battle.Net Armory. Item: " << qPrintable(itemNumber);

			apiPage = QString("https://wowmodelviewer.net/armory.php?item=%1").arg(itemNumber);

			break;
		}
	default:
		LOG_ERROR << "Invalid Import Type: " << type;
		return 3;
	}

	LOG_INFO << "Final API Page:" << qPrintable(apiPage);

	const auto bts = getURLData(apiPage);
	LOG_INFO << bts;
	result = QJsonDocument::fromJson(bts).object();
	return 0;
}

QByteArray ArmoryImporter::getURLData(const QString& inputUrl) const
{
	const QUrl url = QString(inputUrl);
	// LOG_INFO << "Getting info from:" << qPrintable(url.toString());

	if (!url.errorString().isEmpty())
	{
		return QByteArray();
	}

	QNetworkAccessManager manager;
	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", "WoWModelViewer");

	// disable ssl handshake (error when communicating with server as certifcate is self signed)
	auto sslConfiguration = request.sslConfiguration();
	sslConfiguration.setProtocol(QSsl::AnyProtocol);
	sslConfiguration.setPeerVerifyMode(QSslSocket::QueryPeer);
	request.setSslConfiguration(sslConfiguration);
	auto* response = manager.get(request);
	QEventLoop eventLoop;
	connect(response, SIGNAL(finished()), &eventLoop, SLOT(quit()));
	connect(response, SIGNAL(error(QNetworkReply::NetworkError)), &eventLoop, SLOT(quit()));
	eventLoop.exec();

	auto htmldata = response->readAll(); // Source should be stored here

	//LOG_INFO << "Returning Response:" << qPrintable(htmldata);

	return htmldata;
}

bool ArmoryImporter::hasMember(const QJsonValueRef& check, const QString& lookfor)
{
	if (check.toObject().find(lookfor) != check.toObject().end())
		return true;
	return false;
}

bool ArmoryImporter::hasTransmog(const QJsonValueRef& check)
{
	return hasMember(check.toObject()["tooltipParams"], "transmogItem");
}
