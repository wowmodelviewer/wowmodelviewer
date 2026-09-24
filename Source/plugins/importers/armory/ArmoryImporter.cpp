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
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Irrlicht

// Externals

// Other libraries
//#include "charcontrol.h"
#include "ArmoryProxy.h" // link parsing + proxy request
#include "CharInfos.h"
#include "GlobalSettings.h" // Armory proxy URL override
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
  return ((url.indexOf("battle.net") != -1) || (url.indexOf("worldofwarcraft.com") != -1) || (url.indexOf("blizzard.com") != -1));
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

  // Unmapped slots (neck, rings, trinkets, ranged) aren't worn on the model.
  // Return an out-of-range sentinel so the caller skips them -- returning {} would
  // be CS_HEAD (0) and clobber the head item.
  return NUM_CHAR_SLOTS;
}

// What the proxy returns for a character, and where importChar() reads it from.
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
    "name": "D�moniste",
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
        "name": "T�te"
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
        "name": "�paules"
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
        "name": "D�tails de la m�choire",
        "id": 62
      },
      "choice": {
        "name": "Joues n�cros�es",
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
        "name": "D�tails du visage",
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

CharInfos * ArmoryImporter::importChar(QString url) const
{
  auto * result = new CharInfos();

  ArmoryProxy::Character character;
  const ArmoryProxy::Result response = gatherCharacter(url, character);
  const QJsonObject & root = response.json;

  if (response.status == ArmoryProxy::Status::Ok)
  {
    LOG_INFO << "Processing JSON Values...";

    // No Gathering Errors Detected.
    result->equipment.resize(NUM_CHAR_SLOTS);
    result->itemModifierIds.resize(NUM_CHAR_SLOTS);

    // Gather Race & Gender
    auto obj = root.value("playable_race").toObject();
    result->raceId = obj.value("id").toInt();
    obj = root.value("gender").toObject();
    // Use the locale-independent gender "type" ("MALE"/"FEMALE"); the caller compares
    // result->gender against the English "Male" to pick the model sex.
    result->gender = (obj.value("type").toString() == "MALE") ? "Male" : "Female";

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
      if (slot >= NUM_CHAR_SLOTS) // unmapped slot (neck/ring/trinket/ranged) -- not worn
        continue;
      result->equipment[slot] = item["id"].toInt();
      result->itemModifierIds[slot] = item["item_appearance_modifier_id"].toInt();
    }

   
    // Set proper eyeglow. The appearance API nests the class id under "playable_class".
    if (root.value("playable_class").toObject().value("id").toInt() == 6) // 6 = DEATH KNIGHT
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
      result->background = guildTabard.value("background").toObject().value("color").toObject().value("id").toInt();
     
      result->customTabard = true;
    }

    result->valid = true;
  }
  else {
    // Say exactly what went wrong -- which link, which character, which HTTP status --
    // rather than one "could not retrieve" for every kind of failure.
    result->errorMessage = ArmoryProxy::describe(response, character).toStdString();
  }

  return result;
}

ItemRecord * ArmoryImporter::importItem(QString url) const
{
  ItemRecord * result = nullptr;

  // url given is something like http://eu.battle.net/wow/fr/item/104673; only the item
  // number is used (english names only, for now).
  const QString itemNumber = url.mid(url.lastIndexOf("/"));
  LOG_INFO << "Loading Armory item:" << qPrintable(itemNumber);

  const ArmoryProxy::Result response = ArmoryProxy::fetchJson(
    QString("https://wowmodelviewer.net/armory.php?item=%1").arg(itemNumber));

  if (response.status == ArmoryProxy::Status::Ok)
  {
    const QJsonObject & root = response.json;
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

// Fetch a character's appearance document through the proxy. Everything that can go
// wrong -- an unreadable link, an unreachable proxy, a 404 for a hidden profile, a body
// that is valid JSON but holds no character -- comes back as a non-Ok status, so the
// caller can never dress the model from an error response.
ArmoryProxy::Result ArmoryImporter::gatherCharacter(const QString & url, ArmoryProxy::Character & character)
{
  ArmoryProxy::Result response;

  response.status = ArmoryProxy::parseCharacterUrl(url, character);
  if (response.status != ArmoryProxy::Status::Ok)
  {
    LOG_ERROR << "Armory: could not read a character from the link" << qPrintable(url);
    return response;
  }

  LOG_INFO << "Loading Blizzard Armory. Region:" << qPrintable(character.region)
           << ", Realm:" << qPrintable(character.realm)
           << ", Character:" << qPrintable(character.name);

  // The client ships no Blizzard credentials: it calls a proxy that holds them
  // server-side, does the OAuth handshake and returns the appearance JSON. A runtime
  // override (Settings > General) wins over the URL built into this build.
  QString proxyTemplate = QString::fromStdString(GLOBALSETTINGS.armoryProxyURL());
  if (proxyTemplate.isEmpty())
    proxyTemplate = ArmoryProxy::defaultProxyTemplate();

  if (proxyTemplate.isEmpty())
  {
    LOG_ERROR << "Armory: no proxy URL (none built in, and none set in Settings > General).";
    response.status = ArmoryProxy::Status::NoProxy;
    return response;
  }

  const QString requestUrl = ArmoryProxy::buildRequestUrl(proxyTemplate, character);
  LOG_INFO << "Final API Page:" << qPrintable(requestUrl);

  response = ArmoryProxy::fetchJson(requestUrl);
  if (response.status != ArmoryProxy::Status::Ok)
    return response;

  if (!ArmoryProxy::looksLikeAppearance(response.json))
  {
    // A 200 that is not an appearance document: report it instead of importing a
    // character with no race and no items.
    response.status = ArmoryProxy::Status::UnexpectedPayload;
    response.detail = QString("fields returned: %1").arg(QStringList(response.json.keys()).join(", "));
    LOG_ERROR << "Armory: the response is not a character appearance," << qPrintable(response.detail);
  }

  return response;
}

bool ArmoryImporter::hasMember(const QJsonValueRef & check, const QString & lookfor)
{
  if (check.toObject().find(lookfor) != check.toObject().end())
    return true;
  return false;
}

bool ArmoryImporter::hasTransmog(const QJsonValueRef & check)
{
  return hasMember(check.toObject()["tooltipParams"], "transmogItem");
}