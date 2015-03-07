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

// Irrlicht

// Externals
#include <wx/url.h>

// Other libraries
#include "../../../../charcontrol.h"
#include "core/CharInfos.h"
#include "database.h" // ItemRecord
#include "globalvars.h"


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
bool ArmoryImporter::acceptURL(std::string url) const
{
  return (url.find("battle.net") != std::string::npos);
}


CharInfos * ArmoryImporter::importChar(std::string url) const
{
  wxInitialize();

  CharInfos * result = NULL;
  wxJSONValue root;

  if (readJSONValues(CHARACTER,url,root) == 0 && root.Size() != 0)
  {
    // No Gathering Errors Detected.
    result = new CharInfos();

    // Gather Race & Gender
    result->raceId = root[wxT("race")].AsInt();
    result->genderId = root[wxT("gender")].AsInt();
    result->race = wxT("Human");
    result->gender = (result->genderId == 0) ? wxT("Male") : wxT("Female");

    wxJSONValue app = root[wxT("appearance")];
    result->skinColor = app[wxT("skinColor")].AsInt();
    result->faceType = app[wxT("faceVariation")].AsInt();
    result->hairColor = app[wxT("hairColor")].AsInt();
    result->hairStyle = app[wxT("hairVariation")].AsInt();
    result->facialHair = app[wxT("featureVariation")].AsInt();

    // Gather Items
    result->hasTransmogGear = false;
    wxJSONValue items = root[wxT("items")];
    if (items[wxT("back")].Size()>0)
    {
      result->equipment[CS_CAPE] = items[wxT("back")][wxT("id")].AsInt();
      if (items[wxT("back")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_CAPE] = items[wxT("back")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("chest")].Size()>0)
    {
      result->equipment[CS_CHEST] = items[wxT("chest")][wxT("id")].AsInt();
      if (items[wxT("chest")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_CHEST] = items[wxT("chest")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("feet")].Size()>0)
    {
      result->equipment[CS_BOOTS] = items[wxT("feet")][wxT("id")].AsInt();
      if (items[wxT("feet")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_BOOTS] = items[wxT("feet")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("hands")].Size()>0)
    {
      result->equipment[CS_GLOVES] = items[wxT("hands")][wxT("id")].AsInt();
      if (items[wxT("hands")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_GLOVES] = items[wxT("hands")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("head")].Size()>0)
    {
      result->equipment[CS_HEAD] = items[wxT("head")][wxT("id")].AsInt();
      if (items[wxT("head")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_HEAD] = items[wxT("head")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("legs")].Size()>0)
    {
      result->equipment[CS_PANTS] = items[wxT("legs")][wxT("id")].AsInt();
      if (items[wxT("legs")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_PANTS] = items[wxT("legs")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("mainHand")].Size()>0)
    {
      result->equipment[CS_HAND_RIGHT] = items[wxT("mainHand")][wxT("id")].AsInt();
      if (items[wxT("mainHand")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_HAND_RIGHT] = items[wxT("mainHand")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("offHand")].Size()>0)
    {
      result->equipment[CS_HAND_LEFT] = items[wxT("offHand")][wxT("id")].AsInt();
      if (items[wxT("offHand")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_HAND_LEFT] = items[wxT("offHand")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("shirt")].Size()>0)
    {
      result->equipment[CS_SHIRT] = items[wxT("shirt")][wxT("id")].AsInt();
      if (items[wxT("shirt")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_SHIRT] = items[wxT("shirt")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("shoulder")].Size()>0)
    {
      result->equipment[CS_SHOULDER] = items[wxT("shoulder")][wxT("id")].AsInt();
      if (items[wxT("shoulder")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_SHOULDER] = items[wxT("shoulder")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("tabard")].Size()>0)
    {
      result->equipment[CS_TABARD] = items[wxT("tabard")][wxT("id")].AsInt();
      if (items[wxT("tabard")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_TABARD] = items[wxT("tabard")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("waist")].Size()>0)
    {
      result->equipment[CS_BELT] = items[wxT("waist")][wxT("id")].AsInt();
      if (items[wxT("waist")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_BELT] = items[wxT("waist")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }
    if (items[wxT("wrist")].Size()>0)
    {
      result->equipment[CS_BRACERS] = items[wxT("wrist")][wxT("id")].AsInt();
      if (items[wxT("wrist")][wxT("tooltipParams")].HasMember(wxT("transmogItem")))
      {
        result->equipment[CS_BRACERS] = items[wxT("wrist")][wxT("tooltipParams")][wxT("transmogItem")].AsInt();
        result->hasTransmogGear = true;
      }
    }

    // Set proper eyeglow
    if (root[wxT("class")].AsInt() == CLASS_DEATH_KNIGHT)
      result->eyeGlowType = EGT_DEATHKNIGHT;
    else
      result->eyeGlowType = EGT_DEFAULT;

    // tabard (useful if guild tabard)
    wxJSONValue guild = root[wxT("guild")];
    if(guild.Size() > 0)
    {
      wxJSONValue tabard = guild[wxT("emblem")];
      if(tabard.Size() > 0)
      {
        result->tabardIcon = tabard[wxT("icon")].AsInt();
        result->tabardBorder = tabard[wxT("border")].AsInt();
      }
    }


    wxUninitialize();
  }

  return result;
}

ItemRecord * ArmoryImporter::importItem(std::string url) const
{
  wxInitialize();

  wxJSONValue root;
  ItemRecord * result = NULL;

  if (readJSONValues(ITEM,url,root) == 0 && root.Size() != 0)
  {
    // No Gathering Errors Detected.
    result = new ItemRecord();

    // Gather Race & Gender
    result->id = root[wxT("id")].AsInt();
    result->model = root[wxT("displayInfoId")].AsInt();
    root[wxT("name")].AsString(result->name);
    result->itemclass = root[wxT("itemClass")].AsInt();
    result->subclass = root[wxT("itemSubClass")].AsInt();
    result->quality = root[wxT("quality")].AsInt();
    result->type = root[wxT("inventoryType")].AsInt();
  }

  wxUninitialize();
  return result;
}


// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
int ArmoryImporter::readJSONValues(ImportType type, std::string url, wxJSONValue & result) const
{
  wxString apiPage;
  switch(type)
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
        LOG_INFO << wxT("Improperly Formatted URL. Lacks /simple and /advanced");
        return -1;
      }
      wxString strDomain = strURL.Mid(7).BeforeFirst('/');
      wxString strPage = strURL.Mid(7).Mid(strDomain.Len());

      wxString strp = strPage.BeforeLast('/');	// No simple/advanced
      wxString CharName = strp.AfterLast('/');
      strp = strp.BeforeLast('/');				// Update strp
      wxString Realm = strp.AfterLast('/');

      LOG_INFO << "Loading Battle.Net Armory. Site: " << strDomain.c_str() << ", Realm: " << Realm.c_str() << ", Character: " << CharName.c_str();

      apiPage = wxT("http://") + strDomain;
      apiPage << wxT("/api/wow/character/") << Realm << wxT('/') << CharName << wxT("?fields=appearance,items,guild");
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

      wxString strURL(url);
      wxString strDomain = strURL.Mid(7).BeforeFirst('/');
      wxString itemNumber = strURL.Mid(7).AfterLast('/');

      LOG_INFO << "Loading Battle.Net Armory. Site: " << strDomain.c_str() << ", Item: " << itemNumber.c_str();

      apiPage = wxT("http://") + strDomain;
      apiPage << wxT("/api/wow/item/") << itemNumber;

      break;
    }
  }

  LOG_INFO << "Final API Page: " << apiPage.c_str();

  wxJSONReader reader;

  //Read the Armory API Page & get the error numbers
  wxURL apiPageURL(apiPage);
  wxInputStream *doc = apiPageURL.GetInputStream();

  if(!doc)
    return -1;

  return reader.Parse(*doc,&result);
}
