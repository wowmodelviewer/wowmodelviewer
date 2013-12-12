/*
 * ArmoryImporter.cpp
 *
 *  Created on: 9 déc. 2013
 *
 */

#include "ArmoryImporter.h"

#include "charcontrol.h"
#include "CharInfos.h"
#include "globalvars.h"

#include "wx/jsonreader.h"
#include <wx/url.h>

ArmoryImporter::ArmoryImporter()
{

}

ArmoryImporter::~ArmoryImporter()
{

}

CharInfos * ArmoryImporter::importChar(std::string url)
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
				"showCloak":true
			}
		}

		As you can see, this will give us almost all the data we need to properly rebuild the character.

	 */
	wxString strURL(url);

	// Import from http://us.battle.net/wow/en/character/steamwheedle-cartel/Kjasi/simple
	if ((strURL.Mid(7).Find(wxT("simple"))<0)&&(strURL.Mid(7).Find(wxT("advanced"))<0))
	{
		wxMessageBox(wxT("Improperly Formatted URL.\nMake sure your link ends in /simple or /advanced."),wxT("Bad Armory Link"));
		wxLogMessage(wxT("Improperly Formatted URL. Lacks /simple and /advanced"));
		return NULL;
	}
	wxString strDomain = strURL.Mid(7).BeforeFirst('/');
	wxString strPage = strURL.Mid(7).Mid(strDomain.Len());

	wxString strp = strPage.BeforeLast('/');	// No simple/advanced
	wxString CharName = strp.AfterLast('/');
	strp = strp.BeforeLast('/');				// Update strp
	wxString Realm = strp.AfterLast('/');

	wxLogMessage(wxT("Loading Battle.Net Armory. Site: %s, Realm: %s, Character: %s"),strDomain.c_str(),Realm.c_str(),CharName.c_str());

	wxString apiPage = wxT("http://") + strDomain;
	apiPage << wxT("/api/wow/character/") << Realm << wxT('/') << CharName << wxT("?fields=appearance,items");

	wxLogMessage(wxT("Final API Page: %s"),apiPage.c_str());

	// Build the JSON data containers
	wxJSONValue root;
	wxJSONReader reader;

	//Read the Armory API Page & get the error numbers
	wxURL apiPageURL(apiPage);
	wxInputStream *doc = apiPageURL.GetInputStream();

	if(!doc)
		return NULL;

	int numErrors = reader.Parse(*doc,&root);

	if (numErrors == 0 && root.Size() != 0)
	{
		// No Gathering Errors Detected.
		CharInfos * result = new CharInfos();

		// Gather Race & Gender
		result->raceId = root[wxT("race")].AsInt();
		result->genderId = root[wxT("gender")].AsInt();
		result->race = wxT("Human");
		result->gender = (result->genderId == 0) ? wxT("Male") : wxT("Female");
		CharRacesDB::Record racer = racedb.getById(result->raceId);
		if (gameVersion == 30100)
			result->race = racer.getString(CharRacesDB::NameV310);
		else
			result->race = racer.getString(CharRacesDB::Name);
		//wxLogMessage(wxT("RaceID: %i, Race: %s\n          GenderID: %i, Gender: %s"),raceID,race,genderID,gender);

		// Character Details
		result->cd = g_charControl->cd;
		result->cd.race = result->raceId;
		result->cd.gender = result->genderId;
		wxJSONValue app = root[wxT("appearance")];
		result->cd.skinColor = app[wxT("skinColor")].AsInt();
		result->cd.faceType = app[wxT("faceVariation")].AsInt();
		result->cd.hairColor = app[wxT("hairColor")].AsInt();
		result->cd.facialColor = app[wxT("hairColor")].AsInt();
		result->cd.hairStyle = app[wxT("hairVariation")].AsInt();
		result->cd.facialHair = app[wxT("featureVariation")].AsInt();

		// Gather Items
		result->hasTransmogGear = false;
		wxJSONValue items = root[wxT("items")];
		if (items[wxT("back")].Size()>0)
		{
			result->cd.equipment[CS_CAPE] = items[wxT("back")][wxT("id")].AsInt();
			if (items[wxT("back")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_CAPE] = items[wxT("back")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("chest")].Size()>0)
		{
			result->cd.equipment[CS_CHEST] = items[wxT("chest")][wxT("id")].AsInt();
			if (items[wxT("chest")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_CHEST] = items[wxT("chest")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("feet")].Size()>0)
		{
			result->cd.equipment[CS_BOOTS] = items[wxT("feet")][wxT("id")].AsInt();
			if (items[wxT("feet")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_BOOTS] = items[wxT("feet")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("hands")].Size()>0)
		{
			result->cd.equipment[CS_GLOVES] = items[wxT("hands")][wxT("id")].AsInt();
			if (items[wxT("hands")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_GLOVES] = items[wxT("hands")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("head")].Size()>0)
		{
			result->cd.equipment[CS_HEAD] = items[wxT("head")][wxT("id")].AsInt();
			if (items[wxT("head")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_HEAD] = items[wxT("head")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("legs")].Size()>0)
		{
			result->cd.equipment[CS_PANTS] = items[wxT("legs")][wxT("id")].AsInt();
			if (items[wxT("legs")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_PANTS] = items[wxT("legs")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("mainHand")].Size()>0)
		{
			result->cd.equipment[CS_HAND_RIGHT] = items[wxT("mainHand")][wxT("id")].AsInt();
			if (items[wxT("mainHand")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_HAND_RIGHT] = items[wxT("mainHand")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("offHand")].Size()>0)
		{
			result->cd.equipment[CS_HAND_LEFT] = items[wxT("offHand")][wxT("id")].AsInt();
			if (items[wxT("offHand")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_HAND_LEFT] = items[wxT("offHand")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("shirt")].Size()>0)
		{
			result->cd.equipment[CS_SHIRT] = items[wxT("shirt")][wxT("id")].AsInt();
			if (items[wxT("shirt")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_SHIRT] = items[wxT("shirt")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("shoulder")].Size()>0)
		{
			result->cd.equipment[CS_SHOULDER] = items[wxT("shoulder")][wxT("id")].AsInt();
			if (items[wxT("shoulder")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_SHOULDER] = items[wxT("shoulder")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("tabard")].Size()>0)
		{
			result->cd.equipment[CS_TABARD] = items[wxT("tabard")][wxT("id")].AsInt();
			if (items[wxT("tabard")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_TABARD] = items[wxT("tabard")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("waist")].Size()>0)
		{
			result->cd.equipment[CS_BELT] = items[wxT("waist")][wxT("id")].AsInt();
			if (items[wxT("waist")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_BELT] = items[wxT("waist")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}
		if (items[wxT("wrist")].Size()>0)
		{
			result->cd.equipment[CS_BRACERS] = items[wxT("wrist")][wxT("id")].AsInt();
			if (items[wxT("wrist")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
			{
				result->cd.equipment[CS_BRACERS] = items[wxT("wrist")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
				result->hasTransmogGear = true;
			}
		}

		// Set proper eyeglow
		if (root[wxT("class")].AsInt() == CLASS_DEATH_KNIGHT)
			result->cd.eyeGlowType = EGT_DEATHKNIGHT;
		else
			result->cd.eyeGlowType = EGT_DEFAULT;

		return result;
	}

	return NULL;
}
