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

#include "HttpClient.h"
#include "logger/Logger.h"

//#include "charcontrol.h"
#include "CharInfos.h"
#include "database.h" // ItemRecord
#include "wow_enums.h"

#include <sstream>
#include <vector>

ArmoryImporter::ArmoryImporter() = default;

enum
{
	DEBUG_RESULTS = 0
};

bool ArmoryImporter::acceptURL(const std::string& url) const
{
	return (url.find("battle.net") != std::string::npos) ||
		(url.find("worldofwarcraft.com") != std::string::npos) ||
		(url.find("blizzard.com") != std::string::npos);
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

CharInfos* ArmoryImporter::importChar(const std::string& url) const
{
	auto* result = new CharInfos();
	nlohmann::json root;

	const auto readStatus = readJSONValues(CHARACTER, url, root);
	// LOG_INFO << "JSON Read Status:" << readStatus << "Root Count:" << root.count();

	if (readStatus == 0 && root.size() > 0)
	{
		LOG_INFO << "Processing JSON Values...";

		// No Gathering Errors Detected.
		result->equipment.resize(NUM_CHAR_SLOTS);
		result->itemModifierIds.resize(NUM_CHAR_SLOTS);

		// Gather Race & Gender
		result->raceId = root["playable_race"]["id"].get<int>();
		result->gender = root["gender"]["name"].get<std::string>();

		// Gather character customizations
		for (const auto& customization : root["customizations"])
		{
			auto optionid = customization["option"]["id"].get<int>();
			auto choiceid = customization["choice"]["id"].get<int>();
			result->customizations.emplace_back(optionid, choiceid);
		}


		// Gather Items
		result->hasTransmogGear = false;
		for (const auto& item : root["items"])
		{
			const auto slot = armorySlotToCharSlot(item["internal_slot_id"].get<int>());
			result->equipment[slot] = item["id"].get<int>();
			result->itemModifierIds[slot] = item["item_appearance_modifier_id"].get<int>();
		}


		// Set proper eyeglow
		if (root["class"].get<int>() == 6) // 6 = DEATH KNIGHT
			result->eyeGlowType = EGT_DEATHKNIGHT;
		else
			result->eyeGlowType = EGT_DEFAULT;


		// tabard (useful if guild tabard)
		if (root.contains("guild_crest") && !root["guild_crest"].empty())
		{
			const auto& guildTabard = root["guild_crest"];
			result->tabardIcon = guildTabard["emblem"]["id"].get<int>();
			result->iconColor = guildTabard["emblem"]["color"]["id"].get<int>();
			result->tabardBorder = guildTabard["border"]["id"].get<int>();
			result->borderColor = guildTabard["border"]["color"]["id"].get<int>();
			result->background = guildTabard["background"]["color"]["id"].get<int>();

			result->customTabard = true;
		}

		result->valid = true;
	}
	else
	{
		LOG_ERROR << "Bad JSON Results:" << readStatus << "Root Size:" << root.size();
	}

	return result;
}

ItemRecord* ArmoryImporter::importItem(const std::string& url) const
{
	nlohmann::json root;
	ItemRecord* result = nullptr;

	if (readJSONValues(ITEM, url, root) == 0 && root.size() != 0)
	{
		// No Gathering Errors Detected.
		result = new ItemRecord();

		// Gather Race & Gender
		result->id = root["id"].get<int>();
		result->model = root["displayInfoId"].get<int>();
		result->name = root["name"].get<std::string>();
		result->itemclass = root["itemClass"].get<int>();
		result->subclass = root["itemSubClass"].get<int>();
		result->quality = root["quality"].get<int>();
		result->type = root["inventoryType"].get<int>();
	}

	return result;
}

int ArmoryImporter::readJSONValues(ImportType type, const std::string& url, nlohmann::json& result) const
{
	std::string apiPage;
	switch (type)
	{
	case CHARACTER:
		{
			/*
			blizzard's API is mostly RESTful, with data being returned as JSON arrays.
			Full documentation available here: http://blizzard.github.com/api-wow-docs/

			Example: https://eu.api.blizzard.com/profile/wow/character/les-sentinelles/jeromnimo/appearance?namespace=profile-eu&locale=fr_FR

			This will give us all the information we need inside of a JSON array.
			(see JSON sample in previous version)

			As you can see, this will give us almost all the data we need to properly rebuild the character.

			*/

			const auto& strUrl(url);

			std::string region;
			std::string realm;
			std::string charName;

			// Helper to split a string by a delimiter
			auto splitString = [](const std::string& s, char delim) {
				std::vector<std::string> tokens;
				std::istringstream stream(s);
				std::string token;
				while (std::getline(stream, token, delim))
					tokens.push_back(token);
				return tokens;
			};

			// Seems to redirect to worldofwarcraft.com as of Sept 2018.
			if (strUrl.find("battle.net") != std::string::npos)
			{
				// Import from http://us.battle.net/wow/en/character/steamwheedle-cartel/Kjasi/simple

				if ((strUrl.find("simple") == std::string::npos) &&
					(strUrl.find("advanced") == std::string::npos))
				{
					LOG_ERROR << "Improperly Formatted URL. Lacks /simple and /advanced";
					return 2;
				}

				const auto strList = splitString(strUrl.substr(7), '/');

				auto dotPos = strList.at(0).find('.');
				region = (dotPos != std::string::npos) ? strList.at(0).substr(0, dotPos) : strList.at(0);
				realm = strList.at(strList.size() - 3);
				auto qPos = strUrl.rfind('?');
				charName = strList.at(strList.size() - 2);
				if (qPos != std::string::npos)
					charName = charName.substr(0, qPos - 1);
				LOG_INFO << "Battle Net, CharName: " << charName << " Realm: " << realm << " Region: " << region;
			}
			else if ((strUrl.find("worldofwarcraft.com") != std::string::npos) || (url.find("blizzard.com") != std::string::npos))
			{
				// Import from https://worldofwarcraft.com/fr-fr/character/les-sentinelles/jeromnimo
				// or (new form) https://worldofwarcraft.com/fr-fr/character/eu/les-sentinelles/jeromnimo
				// or (new in 2023) https://worldofwarcraft.blizzard.com/en-gb/character/eu/silvermoon/n%C3%A1tnat

				LOG_INFO << strUrl;
				const auto strList = splitString(strUrl.substr(8), '/');

				if (strList.size() > 5) // new form
					region = strList.at(3);
				else
					region = strList.at(1);

				realm = strList.at(strList.size() - 2);
				charName = strList.at(strList.size() - 1);
				auto qPos = charName.rfind('?');
				if (qPos != std::string::npos)
					charName = charName.substr(0, qPos);
				LOG_INFO << "WoW.com, CharName:" << charName << "Realm:" << realm << "Region:" << region;

				// I don't believe these should be translated, as websites tend not to translate URLs...
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
				LOG_ERROR << "Improperly Formatted URL. The domain should be worldofwarcraft.com or blizzard.com";
				return 2;
			}

			LOG_INFO << "Loading Battle.Net Armory. Region:" << region
				<< ", Realm:" << realm
				<< ", Character:" << charName;

			apiPage = "https://wowmodelviewer.net/armory2.php?region=" + region + "&realm=" + realm + "&char=" + charName;
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
			auto lastSlash = strUrl.rfind('/');
			const auto itemNumber = (lastSlash != std::string::npos) ? strUrl.substr(lastSlash) : strUrl;

			LOG_INFO << "Loading Battle.Net Armory. Item: " << itemNumber;

			apiPage = "https://wowmodelviewer.net/armory.php?item=" + itemNumber;

			break;
		}
	default:
		LOG_ERROR << "Invalid Import Type: " << type;
		return 3;
	}

	LOG_INFO << "Final API Page:" << apiPage;

	const auto bts = getURLData(apiPage);
	LOG_INFO << bts;
	result = nlohmann::json::parse(bts, nullptr, false);
	if (result.is_discarded())
		return 1;
	return 0;
}

std::string ArmoryImporter::getURLData(const std::string& inputUrl) const
{
	const auto resp = HttpClient::Get(inputUrl);
	if (!resp.success)
	{
		LOG_ERROR << "HTTP request failed: " << resp.error;
		return {};
	}
	return resp.body;
}

bool ArmoryImporter::hasMember(const nlohmann::json& check, const std::string& lookfor)
{
	return check.contains(lookfor);
}

bool ArmoryImporter::hasTransmog(const nlohmann::json& check)
{
	return check.contains("tooltipParams") && check["tooltipParams"].contains("transmogItem");
}
