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
 * WoWItem.cpp
 *
 *  Created on: 5 feb. 2015
 *      Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#include "WoWItem.h"

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QXmlStreamWriter>


#include "Attachment.h"
#include "database.h" // items
#include "Game.h"
#include "RaceInfos.h"
#include "wow_enums.h"
#include "WoWModel.h"

#include "logger/Logger.h"

std::map<CharSlots, int> WoWItem::SLOT_LAYERS_ = { { CS_SHIRT, 10 }, { CS_HEAD, 11 }, { CS_SHOULDER, 13 },
                                              { CS_PANTS, 10 }, { CS_BOOTS, 11 }, { CS_CHEST, 13 },
                                              { CS_TABARD, 17 }, { CS_BELT, 18 }, { CS_BRACERS, 19 },
                                              { CS_GLOVES, 20 }, { CS_HAND_RIGHT, 21 }, { CS_HAND_LEFT, 22 },
                                              { CS_CAPE, 23 }, { CS_QUIVER, 24 } };


WoWItem::WoWItem(CharSlots slot)
  : slot_(slot)
{
  setName("---- None ----");
}

void WoWItem::setId(int id)
{
  if (id != id_)
  {
    id_ = id;

    if (id_ == 0)
    {
      unload();
      // reset name and quality
      setName("---- None ----");
      quality_ = 0;
      type_ = 0;

      if (slot_ == CS_HAND_RIGHT)
        charModel_->charModelDetails.closeRHand = false;

      if (slot_ == CS_HAND_LEFT)
        charModel_->charModelDetails.closeLHand = false;

      return;
    }

    auto itemlevels = GAMEDATABASE.sqlQuery(QString("SELECT OrderIndex, ItemAppearanceID, ItemAppearanceModifierID FROM ItemModifiedAppearance WHERE ItemID = %1").arg(id));

    if (itemlevels.valid && !itemlevels.values.empty())
    {
      nbLevels_ = 0;
      level_ = 0;
      levelDisplayMap_.clear();
      for (auto& value : itemlevels.values)
      {
        const auto curid = value[1].toInt();
        modifierIdDisplayMap_[value[2].toInt()] = curid;
        // if display id is null (case when item's look doesn't change with level)
        if (curid == 0)
          continue;

        //check if display id already in the map (do not duplicate when look is the same)
        auto found = false;
        for (auto& it : levelDisplayMap_)
        {
          if (it.second == curid)
          {
            found = true;
            break;
          }
        }

        if (!found)
        {
          levelDisplayMap_[nbLevels_] = curid;
          nbLevels_++;
        }
      }
    }

    auto iteminfos = GAMEDATABASE.sqlQuery(QString("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = %1")
                                                   .arg(levelDisplayMap_[level_]));

    if (iteminfos.valid && !iteminfos.values.empty())
      displayId_ = iteminfos.values[0][0].toInt();

    const auto itemRcd = items.getById(id);
    setName(itemRcd.name);
    quality_ = itemRcd.quality;
    type_ = itemRcd.type;
    load();
  }
}

void WoWItem::setDisplayId(int id)
{
  if (displayId_ != id)
  {
    id_ = -1;
    displayId_ = id; // to update from database;
    setName("NPC Item");
    load();
  }
}

void WoWItem::setLevel(int level)
{
  if ((nbLevels_ > 1) && (level_ != level))
  {
    level_ = level;

    auto iteminfos = GAMEDATABASE.sqlQuery(QString("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = %1")
                                                   .arg(levelDisplayMap_[level_]));

    if (iteminfos.valid && !iteminfos.values.empty())
      displayId_ = iteminfos.values[0][0].toInt();

    const auto itemRcd = items.getById(id_);
    setName(itemRcd.name);
    quality_ = itemRcd.quality;
    type_ = itemRcd.type;
    load();
  }
}

void WoWItem::setModifierId(int id)
{
  const auto it = modifierIdDisplayMap_.find(id);
  if (it != modifierIdDisplayMap_.end())
  {
    auto iteminfos = GAMEDATABASE.sqlQuery(QString("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = %1")
                                                   .arg(it->second));

    if (iteminfos.valid && !iteminfos.values.empty())
      displayId_ = iteminfos.values[0][0].toInt();

    const auto itemRcd = items.getById(id_);
    setName(itemRcd.name);
    quality_ = itemRcd.quality;
    type_ = itemRcd.type;
    load();
  }
}

void WoWItem::onParentSet(Component * parent)
{
  charModel_ = dynamic_cast<WoWModel *>(parent);
}

void WoWItem::unload()
{
  // delete models and clear map
  for (auto& itemModel : itemModels_)
    delete itemModel.second;

  itemModels_.clear();

  // release textures and clear map
  for (auto& itemTexture : itemTextures_)
    TEXTUREMANAGER.delbyname(itemTexture.second->fullname());

  itemTextures_.clear();

  // clear map
  itemGeosets_.clear();

  // remove any existing attachement
  if (charModel_->attachment)
    charModel_->attachment->delSlot(slot_);

  // unload any merged model
  if (mergedModel_ != nullptr)
  {
    // TODO : unmerge trigs refreshMerging that trigs refresh... so mergedModel_ must be null...
    // need to find a better way to solve this
    const auto m = mergedModel_;
    mergedModel_ = nullptr;
    charModel_->unmergeModel(m);
    delete mergedModel_;
  }
}

void WoWItem::load()
{
  unload();

  if (!charModel_) // no parent => give up
    return;

  if (id_ == 0 || displayId_ == 0) // no equipment, just return
    return;

  const auto charInfos = charModel_->infos;

  sqlResult iteminfos;

  // query geosets infos
  if (!queryItemInfo(QString("SELECT GeoSetGroup1, GeoSetGroup2, GeoSetGroup3, GeoSetGroup4, GeoSetGroup5, GeoSetGroup6, "
                             "AttachmentGeoSetGroup1, AttachmentGeoSetGroup2, AttachmentGeoSetGroup3, "
                             "AttachmentGeoSetGroup4, AttachmentGeoSetGroup5, AttachmentGeoSetGroup6, Flags "
                             "FROM ItemDisplayInfo WHERE ItemDisplayInfo.ID = %1").arg(displayId_), 
                     iteminfos))
    return;

  int geosetGroup[6] = { iteminfos.values[0][0].toInt(), iteminfos.values[0][1].toInt() ,
                         iteminfos.values[0][2].toInt(), iteminfos.values[0][3].toInt() ,
                         iteminfos.values[0][4].toInt(), iteminfos.values[0][5].toInt() };

  int attachmentGeosetGroup[6] =
                       { iteminfos.values[0][6].toInt(), iteminfos.values[0][7].toInt() ,
                         iteminfos.values[0][8].toInt(), iteminfos.values[0][9].toInt() ,
                         iteminfos.values[0][10].toInt(), iteminfos.values[0][11].toInt() };
  
  displayFlags_ = iteminfos.values[0][12].toInt();
                         
  // query models
  int models[2] = { getCustomModelId(0), getCustomModelId(1) };

  // query textures
  int textures[2] = { getCustomTextureId(0), getCustomTextureId(1) };

  // query textures from ItemDisplayInfoMaterialRes (if relevant)
  auto texinfos = GAMEDATABASE.sqlQuery(QString("SELECT * FROM ItemDisplayInfoMaterialRes WHERE ItemDisplayInfoID = %1").arg(displayId_));
  if (texinfos.valid && !texinfos.empty())
  {
    auto classFilter = QString("ComponentTextureFileData.ClassID = %1").arg(CLASS_ANY);
    if (charModel_ && charModel_->cd.isDemonHunter())
      classFilter = QString("(ComponentTextureFileData.ClassID = %1 OR ComponentTextureFileData.ClassID = %2)").arg(CLASS_DEMONHUNTER).arg(CLASS_ANY);
    
    if (queryItemInfo(QString("SELECT FileDataID FROM ItemDisplayInfoMaterialRes "
                              "LEFT JOIN TextureFileData ON ItemDisplayInfoMaterialRes.MaterialResourcesID = TextureFileData.MaterialResourcesID "
                              "INNER JOIN ComponentTextureFileData ON ComponentTextureFileData.ID = TextureFileData.FileDataID "
                              "WHERE (ComponentTextureFileData.GenderIndex = %1 OR ComponentTextureFileData.GenderIndex = %2) "
                              "AND ItemDisplayInfoID = %3 AND %4 "
                              "ORDER BY ComponentTextureFileData.GenderIndex, ComponentTextureFileData.ClassID DESC")

                              .arg(GENDER_ANY).arg(charInfos.sexID).arg(displayId_).arg(classFilter),
                      iteminfos))
    {
      for (auto& value : iteminfos.values)
      {
        const auto tex = GAMEDIRECTORY.getFile(value[0].toInt());
        if (tex)
        {
          auto texRegion = getRegionForTexture(tex);
          // Only add one texture per region (first one in sort order):
          if (itemTextures_.count(texRegion) < 1)
          {
            TEXTUREMANAGER.add(tex);
            itemTextures_[texRegion] = tex;
          }
        }
      }
    }
  }

  switch (slot_)
  {
    case CS_HEAD:
    {
      // attachments
      updateItemModel(ATT_HELMET, models[0], textures[0]);

      // geosets
      // Head: {geosetGroup[0] = 2700**, geosetGroup[1] = 2101 }
      itemGeosets_[CG_GEOSET2700] = 1 + geosetGroup[0];
      itemGeosets_[CG_GEOSET2100] = 1 + geosetGroup[1];
      
      // 'collections' models:
      if (models[1] != 0)
      {
        mergeModel(CS_HEAD, models[1], textures[1]);
        mergedModel_->setGeosetGroupDisplay(CG_GEOSET2700, 1 + attachmentGeosetGroup[0]);
        mergedModel_->setGeosetGroupDisplay(CG_GEOSET2100, 1 + attachmentGeosetGroup[1]);
      }
      
      break;
    }
    case CS_SHOULDER:
    {
      // geosets
      // Shoulder: {geosetGroup[0] = 2601}
      itemGeosets_[CG_GEOSET2600] = 1 + geosetGroup[0];

      // find position index value from ComponentModelFileData table
      const auto query = QString("SELECT ID, PositionIndex FROM ComponentModelFileData "
                                 "WHERE ID IN (%1,%2)").arg(models[0]).arg(models[1]);

      auto result = GAMEDATABASE.sqlQuery(query);
      
      auto leftIndex = 0;
      auto rightIndex = 1;
      if (result.valid && !result.values.empty())
      {
        const auto modelid = result.values[0][0].toInt();
        const auto position = result.values[0][1].toInt();
        
        if (modelid == models[0])
        {
          if (position == 0)
          {
            leftIndex = 0;
            rightIndex = 1;
          }
          else
          {
            leftIndex = 1;
            rightIndex = 0;
          }
        }
        else
        {
          if (position == 0)
          {
            leftIndex = 1;
            rightIndex = 0;
          }
          else
          {
            leftIndex = 0;
            rightIndex = 1;
          }
        }
      }
      else
      {
        LOG_ERROR << "Impossible to query information for item" << name() << "(id " << id_ << "- display id" << displayId_ << ") - SQL ERROR";
        LOG_ERROR << query;
      }

      LOG_INFO << "leftIndex" << leftIndex << "rightIndex" << rightIndex;

      // left shoulder
      updateItemModel(ATT_LEFT_SHOULDER, models[leftIndex], textures[leftIndex]);

      // right shoulder
      updateItemModel(ATT_RIGHT_SHOULDER, models[rightIndex], textures[rightIndex]);
      
      break;
    }
    case CS_BOOTS:
    {
      // geosets
      // Boots: {geosetGroup[0] = 501, geosetGroup[1] = 2000*}
      itemGeosets_[CG_BOOTS] = 1 + geosetGroup[0];
      // geoset group 20 (CG_FEET) is handled a bit differently, according to wowdev.wiki:
      if (geosetGroup[1] == 0)
        itemGeosets_[CG_FEET] = 2;
      else if (geosetGroup[1] > 0)
        itemGeosets_[CG_FEET] = geosetGroup[1];
      // else ? should we do anything if geosetGroup[1] < 0?

      // 'collections' models:
      if (models[0] != 0)
      {
        mergeModel(CS_BOOTS, models[0], textures[0]);
        mergedModel_->setGeosetGroupDisplay(CG_BOOTS, 1 + attachmentGeosetGroup[0]);
        mergedModel_->setGeosetGroupDisplay(CG_FEET, 1 + attachmentGeosetGroup[1]);
      }

      break;
    }
    case CS_BELT:
    {
 
      // geosets
      // Waist: {geosetGroup[0] = 1801}
      itemGeosets_[CG_BELT] = 1 + geosetGroup[0];

      // buckle model
      updateItemModel(ATT_BELT_BUCKLE, models[0], textures[0]);
      
      // 'collections' models:
      if (models[1] != 0)
      {
        mergeModel(CS_BELT, models[1], textures[1]);
        mergedModel_->setGeosetGroupDisplay(CG_BELT, 1 + attachmentGeosetGroup[0]);
      }
     
      break;
    }
    case CS_PANTS:
    {
      // geosets
      // Pants: {geosetGroup[0] = 1101, geosetGroup[1] = 901, geosetGroup[2] = 1301}
      itemGeosets_[CG_PANTS] = 1 + geosetGroup[0];
      itemGeosets_[CG_KNEEPADS] = 1 + geosetGroup[1];
      itemGeosets_[CG_TROUSERS] = 1 + geosetGroup[2];
      
      // 'collections' models:
      if (models[0] != 0)
      {
        mergeModel(CS_PANTS, models[0], textures[0]);
        mergedModel_->setGeosetGroupDisplay(CG_PANTS, 1 + attachmentGeosetGroup[0]);
        mergedModel_->setGeosetGroupDisplay(CG_KNEEPADS, 1 + attachmentGeosetGroup[1]);
        mergedModel_->setGeosetGroupDisplay(CG_TROUSERS, 1 + attachmentGeosetGroup[2]);
      }

      break;
    }
    case CS_SHIRT:
    case CS_CHEST:
    {
      // geosets
      // Chest: {geosetGroup[0] = 801, geosetGroup[1] = 1001, geosetGroup[2] = 1301, geosetGroup[3] = 2201, geosetGroup[4] = 2801}
      itemGeosets_[CG_SLEEVES] = 1 + geosetGroup[0];
      itemGeosets_[CG_CHEST] = 1 + geosetGroup[1];
      itemGeosets_[CG_TROUSERS] = 1 + geosetGroup[2];
      itemGeosets_[CG_TORSO] = 1 + geosetGroup[3];
      itemGeosets_[CG_GEOSET2800] = 1 + geosetGroup[4];
      
      // 'collections' models:
      if (models[0] != 0)
      {
        mergeModel(CS_CHEST, models[0], textures[0]);
        mergedModel_->setGeosetGroupDisplay(CG_SLEEVES, 1 + attachmentGeosetGroup[0]);
        mergedModel_->setGeosetGroupDisplay(CG_CHEST, 1 + attachmentGeosetGroup[1]);
        mergedModel_->setGeosetGroupDisplay(CG_TROUSERS, 1 + attachmentGeosetGroup[2]);
        mergedModel_->setGeosetGroupDisplay(CG_TORSO, 1 + attachmentGeosetGroup[3]);
        mergedModel_->setGeosetGroupDisplay(CG_GEOSET2800, 1 + attachmentGeosetGroup[4]);
      }
      
      break;
    }
    case CS_BRACERS:
    {
      // nothing specific for bracers
      break;
    }
    case CS_GLOVES:
    {
      // geosets 
      // Gloves: {geosetGroup[0] = 401, geosetGroup[1] = 2301}
      itemGeosets_[CG_GLOVES] = 1 + geosetGroup[0];
      itemGeosets_[CG_HAND_ATTACHMENT] = 1 + geosetGroup[1];
      
      // 'collections' models:
      if (models[0] != 0)
      {
        mergeModel(CS_GLOVES, models[0], textures[0]);
        mergedModel_->setGeosetGroupDisplay(CG_GLOVES, 1 + attachmentGeosetGroup[0]);
        mergedModel_->setGeosetGroupDisplay(CG_HAND_ATTACHMENT, 1 + attachmentGeosetGroup[1]);
      }
    
      break;
    }
    case CS_HAND_RIGHT:
    case CS_HAND_LEFT:
    {
      updateItemModel(((slot_ == CS_HAND_RIGHT) ? ATT_RIGHT_PALM : ATT_LEFT_PALM), models[0], textures[0]);
      break;
    }
    case CS_CAPE:
    {
      auto * tex = GAMEDIRECTORY.getFile(textures[0]);
      if (tex)
      {
        TEXTUREMANAGER.add(tex);
        itemTextures_[getRegionForTexture(tex)] = tex;
      }
      
      // geosets
      // Cape: {geosetGroup[0] = 1501}
      itemGeosets_[CG_CLOAK] = 1 + geosetGroup[0];
      
      // 'collections' models:
      if (models[0] != 0)
      {
        mergeModel(CS_CAPE, models[0], textures[0]);
        mergedModel_->setGeosetGroupDisplay(CG_CLOAK, 1 + attachmentGeosetGroup[0]);
      }
      
      break;
    }
    case CS_TABARD:
    {
      if (isCustomizableTabard())
      {
        charModel_->td.showCustom = true;
        itemGeosets_[CG_TABARD] = 2;

        auto * texture = charModel_->td.GetBackgroundTex(CR_TORSO_UPPER);
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          itemTextures_[CR_TABARD_1] = texture;
        }

        texture = charModel_->td.GetBackgroundTex(CR_TORSO_LOWER);
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          itemTextures_[CR_TABARD_2] = texture;
        }

        texture = charModel_->td.GetIconTex(CR_TORSO_UPPER);
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          itemTextures_[CR_TABARD_3] = texture;
        }

        texture = charModel_->td.GetIconTex(CR_TORSO_LOWER);
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          itemTextures_[CR_TABARD_4] = texture;
        }

        texture = charModel_->td.GetBorderTex(CR_TORSO_UPPER);
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          itemTextures_[CR_TABARD_5] = texture;
        }

        texture = charModel_->td.GetBorderTex(CR_TORSO_LOWER);
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          itemTextures_[CR_TABARD_6] = texture;
        }
      }
      else
      {
        charModel_->td.showCustom = false;

        // geosets
        // Tabard: {geosetGroup[0] = 1201}
        itemGeosets_[CG_TABARD] = 1 + geosetGroup[0];
      }

      break;
    }
    case CS_QUIVER:
      break;
    default:
      break;
  }
}

void WoWItem::refresh()
{
  if (id_ == 0) // no item equipped, give up
    return;

  // merge model if any
  if (mergedModel_ != nullptr)
    charModel_->mergeModel(mergedModel_, -1);

  // update geoset values
  for (const auto it : itemGeosets_)
  {
    if ((slot_ != CS_BOOTS) && // treat boots geoset in a special case - cf CS_BOOTS
        (slot_ != CS_PANTS)) // treat trousers geoset in a special case - cf CS_PANTS
    {
      charModel_->cd.geosets[it.first] = it.second;
      /*
      if (mergedModel_ != 0)
        mergedModel_->setGeosetGroupDisplay(it.first, 1);
      */
    }
  }

  // attach items if any
  if (charModel_->attachment)
  {
    if ((slot_ != CS_HAND_RIGHT) && // treat right hand attachment in a special case - cf CS_HAND_RIGHT
        (slot_ != CS_HAND_LEFT))    // treat left hand attachment in a special case - cf CS_HAND_LEFT
    {
      charModel_->attachment->delSlot(slot_);
      for (const auto it : itemModels_)
        charModel_->attachment->addChild(it.second, it.first, slot_);
    }
  }

  // add textures if any
  if ((slot_ != CS_BOOTS) &&  // treat boots texturing in a special case - cf CS_BOOTS
      (slot_ != CS_GLOVES) && // treat gloves texturing in a special case - cf CS_GLOVES 
      (slot_ != CS_TABARD) && // treat tabard texturing in a special case - cf CS_TABARD 
      (slot_ != CS_CAPE))     // treat cape texturing in a special case - cf CS_CAPE 
  {
    for (const auto it : itemTextures_)
      charModel_->tex.addLayer(it.second, it.first, SLOT_LAYERS_[slot_]);
  }
  

  switch (slot_)
  {
    case CS_HEAD:
    {
      // nothing specific for head items
      break;
    }
    case CS_SHOULDER:
    {
      // nothing specific for shoulder items
      break;
    }
    case CS_HAND_RIGHT:
    {
      if (charModel_->attachment)
      {
        charModel_->attachment->delSlot(CS_HAND_RIGHT);

        const auto it = itemModels_.find(ATT_RIGHT_PALM);
        if (it != itemModels_.end())
        {
          int attachement = ATT_RIGHT_PALM;
          const auto &item = items.getById(id_);
          if (charModel_->bSheathe &&  item.sheath != SHEATHETYPE_NONE)
          {
            // make the weapon cross
            if (item.sheath == ATT_LEFT_BACK_SHEATH)
              attachement = ATT_RIGHT_BACK_SHEATH;
            if (item.sheath == ATT_LEFT_BACK)
              attachement = ATT_RIGHT_BACK;
            if (item.sheath == ATT_LEFT_HIP_SHEATH)
              attachement = ATT_RIGHT_HIP_SHEATH;
          }

          if (charModel_->bSheathe)
            charModel_->charModelDetails.closeRHand = false;
          else
            charModel_->charModelDetails.closeRHand = true;

          charModel_->attachment->addChild(it->second, attachement, slot_);
        }
      }
      break;
    }
    case CS_HAND_LEFT:
    {
      if (charModel_->attachment)
      {
        charModel_->attachment->delSlot(CS_HAND_LEFT);

        const auto it = itemModels_.find(ATT_LEFT_PALM);
        if (it != itemModels_.end())
        {
          const auto &item = items.getById(id_);
          int attachement = ATT_LEFT_PALM;

          if (item.type == IT_SHIELD)
            attachement = ATT_LEFT_WRIST;

          if (charModel_->bSheathe &&  item.sheath != SHEATHETYPE_NONE)
            attachement = static_cast<POSITION_SLOTS>(item.sheath);

          if (charModel_->bSheathe || item.type == IT_SHIELD)
            charModel_->charModelDetails.closeLHand = false;
          else
            charModel_->charModelDetails.closeLHand = true;

          // if (displayFlags_ & 0x100) then item should be mirrored when in left hand:
          it->second->mirrored_ = (displayFlags_ & 0x100);
          charModel_->attachment->addChild(it->second, attachement, slot_);
        }
      }
      break;
    }
    case CS_BELT:
    {
      // nothing specific for belt items
      break;
    }
    case CS_BOOTS:
    {
      for (const auto it : itemGeosets_)
      {
        if (it.first != CG_BOOTS && !charModel_->isWearingARobe())
        {
          charModel_->cd.geosets[it.first] = it.second;
          /*
          if (mergedModel_ != 0)
            mergedModel_->setGeosetGroupDisplay(it.first, 1);
          */
        }
        else
        {
          // don't render boots behind robe
          if (!charModel_->isWearingARobe())
          {
            charModel_->cd.geosets[it.first] = it.second;
            /*
            if (mergedModel_ != 0)
              mergedModel_->setGeosetGroupDisplay(CG_BOOTS, 1);
            */
          }
        }
      }

      auto texIt = itemTextures_.find(CR_LEG_LOWER);
      if (texIt != itemTextures_.end())
        charModel_->tex.addLayer(texIt->second, CR_LEG_LOWER, SLOT_LAYERS_[slot_]);

      if (!charModel_->cd.showFeet)
      {
        texIt = itemTextures_.find(CR_FOOT);
        if (texIt != itemTextures_.end())
          charModel_->tex.addLayer(texIt->second, CR_FOOT, SLOT_LAYERS_[slot_]);
      }
      break;
    }
    case CS_PANTS:
    {
      for (const auto it : itemGeosets_)
      {
        if (it.first != CG_TROUSERS)
        {
          charModel_->cd.geosets[it.first] = it.second;
          /*
          if (mergedModel_ != 0)
            mergedModel_->setGeosetGroupDisplay(it.first, 1);
          */
        }
      }

      const auto geoIt = itemGeosets_.find(CG_TROUSERS);

      if (geoIt != itemGeosets_.end())
      {
        // apply trousers geosets only if character is not already wearing a robe
        if(!charModel_->isWearingARobe())
        {
          charModel_->cd.geosets[CG_TROUSERS] = geoIt->second;
          /*
          if (mergedModel_)
            mergedModel_->setGeosetGroupDisplay(CG_TROUSERS, 1);
          */
        }
      }

      break;
    }
    case CS_SHIRT:
    case CS_CHEST:
    {
      // nothing specific for shirt & chest items
      break;
    }
    case CS_BRACERS:
    {
      // nothing specific for bracers items
      break;
    }
    case CS_GLOVES:
    {
      auto texIt = itemTextures_.find(CR_ARM_LOWER);

      auto layer = SLOT_LAYERS_[slot_];

      // if we are wearing a robe, render gloves first in texture compositing
      // only if GeoSetGroup1 is 0 (from item displayInfo db) which corresponds to stored geoset equals to 1
      if (charModel_->isWearingARobe() && (charModel_->cd.geosets[CG_GLOVES] == 1))
        layer = SLOT_LAYERS_[CS_CHEST] - 1;

      if (texIt != itemTextures_.end())
        charModel_->tex.addLayer(texIt->second, CR_ARM_LOWER, layer);

      texIt = itemTextures_.find(CR_HAND);
      if (texIt != itemTextures_.end())
        charModel_->tex.addLayer(texIt->second, CR_HAND, layer);
      break;
    }
    case CS_CAPE:
    {
      const auto it = itemTextures_.find(CR_CAPE);
      if (it != itemTextures_.end())
        charModel_->updateTextureList(it->second, TEXTURE_OBJECT_SKIN);
      break;
    }
    case CS_TABARD:
    {
      if (isCustomizableTabard())
      {
        auto it = itemTextures_.find(CR_TABARD_1);
        if (it != itemTextures_.end())
          charModel_->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS_[slot_]);

        it = itemTextures_.find(CR_TABARD_2);
        if (it != itemTextures_.end())
          charModel_->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS_[slot_]);

        it = itemTextures_.find(CR_TABARD_3);
        if (it != itemTextures_.end())
          charModel_->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS_[slot_]);

        it = itemTextures_.find(CR_TABARD_4);
        if (it != itemTextures_.end())
          charModel_->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS_[slot_]);

        it = itemTextures_.find(CR_TABARD_5);
        if (it != itemTextures_.end())
          charModel_->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS_[slot_]);

        it = itemTextures_.find(CR_TABARD_6);
        if (it != itemTextures_.end())
          charModel_->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS_[slot_]);

      }
      else
      {
        auto it = itemTextures_.find(CR_TORSO_UPPER);
        if (it != itemTextures_.end())
          charModel_->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS_[slot_]);

        it = itemTextures_.find(CR_TORSO_LOWER);
        if (it != itemTextures_.end())
          charModel_->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS_[slot_]);
      }
      break;
    }
    default:
      break;
  }
}

bool WoWItem::isCustomizableTabard() const
{
  return (id_ == 5976 || // Guild Tabard
          id_ == 69209 || // Illustrious Guild Tabard
          id_ == 69210);  // Renowned Guild Tabard
}

void WoWItem::save(QXmlStreamWriter & stream) const 
{
  stream.writeStartElement("item");

  stream.writeStartElement("slot");
  stream.writeAttribute("value", QString::number(slot_));
  stream.writeEndElement();

  stream.writeStartElement("id");
  stream.writeAttribute("value", QString::number(id_));
  stream.writeEndElement();

  stream.writeStartElement("displayId");
  stream.writeAttribute("value", QString::number(displayId_));
  stream.writeEndElement();

  stream.writeStartElement("level");
  stream.writeAttribute("value", QString::number(level_));
  stream.writeEndElement();

  if (isCustomizableTabard())
    charModel_->td.save(stream);

  stream.writeEndElement(); // item
}

void WoWItem::load(QString & f)
{
  QFile file(f);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open" << f;
    return;
  }

  QXmlStreamReader reader;
  reader.setDevice(&file);

  auto nbValuesRead = 0;
  while (!reader.atEnd() && nbValuesRead != 3)
  {
    if (reader.isStartElement())
    {
      if (reader.name() == "slot")
      {
        const auto slot = reader.attributes().value("value").toString().toUInt();

        if (slot == slot_)
        {
          while (!reader.atEnd() && nbValuesRead != 3)
          {
            if (reader.isStartElement())
            {
              if (reader.name() == "id")
              {
                const auto id = reader.attributes().value("value").toString().toInt();
                nbValuesRead++;
                if (id != -1)
                  setId(id);
              }

              if (reader.name() == "displayId")
              {
                const auto id = reader.attributes().value("value").toString().toInt();
                nbValuesRead++;
                if (id_ == -1)
                  setDisplayId(id);
              }

              if (reader.name() == "level")
              {
                const auto level = reader.attributes().value("value").toString().toInt();
                nbValuesRead++;
                setLevel(level);
              }
            }
            reader.readNext();
          }
        }
      }
    }
    reader.readNext();
  }

  if (isCustomizableTabard()) // look for extra tabard details
  {
    reader.readNext();
    while (reader.isStartElement() == false)
      reader.readNext();

    if (reader.name() == "TabardDetails")
    {
      charModel_->td.load(reader);
      load(); // refresh tabard textures
    }
  }
}

void WoWItem::updateItemModel(POSITION_SLOTS pos, int modelId, int textureId)
{
  if (modelId == 0)
    return;

  auto *m = new WoWModel(GAMEDIRECTORY.getFile(modelId), true);

  if (m->ok)
  {
    for (uint i = 0; i < m->geosets.size(); i++)
      m->showGeoset(i, true);

    itemModels_[pos] = m;
    auto * texture = GAMEDIRECTORY.getFile(textureId);
    if (texture)
      m->updateTextureList(texture, TEXTURE_OBJECT_SKIN);
    else
      LOG_ERROR << "Error during item update" << id_ << "(display id" << displayId_ << "). Texture" << textureId << "can't be loaded";
  }
  else
  {
    LOG_ERROR << "Error during item update" << id_ << "(display id" << displayId_ << "). Model" << modelId << "can't be loaded";
  }
}

void WoWItem::mergeModel(CharSlots slot, int modelId, int textureId)
{
  if (modelId == 0)
    return;

  mergedModel_ = new WoWModel(GAMEDIRECTORY.getFile(modelId), true);

  if (mergedModel_->ok)
  {
    auto * texture = GAMEDIRECTORY.getFile(textureId);
    if (texture)
    {
      mergedModel_->updateTextureList(texture, TEXTURE_OBJECT_SKIN);
      charModel_->updateTextureList(texture, TEXTURE_OBJECT_SKIN);
    }
    else
      LOG_ERROR << "Error during item update" << id_ << "(display id" << displayId_ << "). Texture" << textureId << "can't be loaded";

    for (uint i = 0; i < mergedModel_->geosets.size(); i++)
      mergedModel_->hideAllGeosets();
  }
  else
  {
    LOG_ERROR << "Error during item update" << id_ << "(display id" << displayId_ << "). Model" << modelId << "can't be loaded";
  }
}

CharRegions WoWItem::getRegionForTexture(GameFile * file) const
{
  auto result = CR_UNK8;

  if (file)
  {
    const auto fullname = file->fullname();

    if (fullname.contains("armlowertexture", Qt::CaseInsensitive))
    {
      result = CR_ARM_LOWER;
    }
    else if (fullname.contains("armuppertexture", Qt::CaseInsensitive))
    {
      result = CR_ARM_UPPER;
    }
    else if (fullname.contains("foottexture", Qt::CaseInsensitive))
    {
      result = CR_FOOT;
    }
    else if (fullname.contains("handtexture", Qt::CaseInsensitive))
    {
      result = CR_HAND;
    }
    else if (fullname.contains("leglowertexture", Qt::CaseInsensitive))
    {
      result = CR_LEG_LOWER;
    }
    else if (fullname.contains("leguppertexture", Qt::CaseInsensitive))
    {
      result = CR_LEG_UPPER;
    }
    else if (fullname.contains("torsolowertexture", Qt::CaseInsensitive))
    {
      result = CR_TORSO_LOWER;
    }
    else if (fullname.contains("torsouppertexture", Qt::CaseInsensitive))
    {
      result = CR_TORSO_UPPER;
    }
    else if (fullname.contains("cape", Qt::CaseInsensitive))
    {
      result = CR_CAPE;
    }
    else
    {
      LOG_ERROR << "Unable to determine region for texture" << fullname << " - item" << id_ << "displayid" << displayId_;
    }
  }

  return result;
}

bool WoWItem::queryItemInfo(const QString & query, sqlResult & result) const
{
  result = GAMEDATABASE.sqlQuery(query);
 
  if (!result.valid || result.values.empty())
  {
    LOG_ERROR << "Impossible to query information for item" << name() << "(id " << id_ << "- display id" << displayId_ << ") - SQL ERROR";
    LOG_ERROR << query;
    return false;
  }

  return true;
}

int WoWItem::getCustomModelId(size_t index) const 
{
  sqlResult infos;
  if (!queryItemInfo(QString("SELECT FileDataID FROM ItemDisplayInfo "
                             "LEFT JOIN ModelFileData ON %1 = ModelFileData.ModelResourcesID "
                             "WHERE ItemDisplayInfo.ID = %2").arg((index == 0)?"ModelResourcesID1":"ModelResourcesID2").arg(displayId_),
                     infos))
    return 0;

  // if there is only one result, return model id:
  if (infos.values.size() == 1)
    return infos.values[0][0].toInt();

  // if there are multiple values, check by race and sex:
  QStringList idList;
  for (auto it : infos.values)
    idList << it[0];
  auto idListStr = idList.join(", ");
  idListStr = "(" + idListStr + ")";
  
  const auto charInfos = charModel_->infos;

  auto classFilter = QString("ClassID = %1").arg(CLASS_ANY);
  if (charModel_ && charModel_->cd.isDemonHunter())
    classFilter = QString("(ClassID = %1 OR ClassID = %2)").arg(CLASS_DEMONHUNTER).arg(CLASS_ANY);
  
  // It looks like shoulders are always in pairs, with PositionIndex values 0 and 1.
  // Depending on index (model 1 or 2) we sort the PositionIndex differently so one will
  // return left and one right shoulder. Noting this in case in the future it turns out
  // this assumption isn't always right - Wain
  const QString positionSort = ((index == 0) ? "" : "DESC");
  
  // Order all queries by GenderIndex to ensure definite genders have priority over generic ones,
  // and ClassID descending to ensure Demon Hunter textures have priority over regular ones, for DHs only:
  sqlResult iteminfos;
  QString query = QString("SELECT ID, PositionIndex FROM ComponentModelFileData "
                          "WHERE RaceID = %1 AND (GenderIndex = %2 OR GenderIndex = %3) "
                          "AND ID IN %4 AND %5 "
                          "ORDER BY GenderIndex, ClassID DESC, PositionIndex %6")
                          .arg(charInfos.raceID).arg(charInfos.sexID).arg(GENDER_ANY).arg(idListStr).arg(classFilter).arg(positionSort);
  if (queryItemInfo(query, iteminfos))
    return iteminfos.values[0][0].toInt();

  // Failed to find model for that specific race and sex, so check fallback race:
  if (charInfos.modelFallbackRaceID > 0)
  {
    query = QString("SELECT ID, PositionIndex FROM ComponentModelFileData "
                    "WHERE RaceID = %1 AND (GenderIndex = %2 OR GenderIndex = %3) "
                    "AND ID IN %4 AND %5 "
                    "ORDER BY GenderIndex, ClassID DESC, PositionIndex %6")
                    .arg(charInfos.modelFallbackRaceID).arg(charInfos.modelFallbackSexID).arg(GENDER_NONE).arg(idListStr).arg(classFilter).arg(positionSort);

    if (queryItemInfo(query, iteminfos))
      return iteminfos.values[0][0].toInt();
  }
  
  // We still didn't find the model, so check for RACE_ANY (race = 0) items:
  // Note: currently all race = 0 entries are also gender = 2, but we probably
  // shouldn't assume it will stay that way, so check for both gender values:
  query = QString("SELECT ID, PositionIndex FROM ComponentModelFileData "
                  "WHERE RaceID = %1 AND (GenderIndex = %2 OR GenderIndex = %3) "
                  "AND ID IN %4 AND %5 "
                  "ORDER BY GenderIndex, ClassID DESC, PositionIndex %6")
                  .arg(RACE_ANY).arg(charInfos.modelFallbackSexID).arg(GENDER_NONE).arg(idListStr).arg(classFilter).arg(positionSort);

  if (queryItemInfo(query, iteminfos))
    return iteminfos.values[0][0].toInt();

  return 0;
}

int WoWItem::getCustomTextureId(size_t index) const
{
  sqlResult infos;
  if (!queryItemInfo(QString("SELECT FileDataID FROM ItemDisplayInfo "
                             "LEFT JOIN TextureFileData ON %1 = TextureFileData.MaterialResourcesID "
                             "WHERE ItemDisplayInfo.ID = %2").arg((index == 0)?"ModelMaterialResourcesID1":"ModelMaterialResourcesID2").arg(displayId_),
                     infos))
    return 0;

  // if there is only one result, return texture id:
  if (infos.values.size() == 1)
    return infos.values[0][0].toInt();

  // if there are multiple values, check by race and sex:
  QStringList idList;
  for (auto it : infos.values)
    idList << it[0];
  auto idListStr = idList.join(", ");
  idListStr = "(" + idListStr + ")";
  
  const auto charInfos = charModel_->infos;
  
  QString classFilter = QString("ClassID = %1").arg(CLASS_ANY);

  if (charModel_ && charModel_->cd.isDemonHunter())
    classFilter = QString("(ClassID = %1 OR ClassID = %2)").arg(CLASS_DEMONHUNTER).arg(CLASS_ANY);
  
  // Order all queries by GenderIndex to ensure definite genders have priority over generic ones,
  // and ClassID descending to ensure Demon Hunter textures have priority over regular ones, for DHs only:
  sqlResult iteminfos; 
  QString query = QString("SELECT ID FROM ComponentTextureFileData "
                          "WHERE RaceID = %1 AND (GenderIndex = %2 OR GenderIndex = %3) "
                          "AND ID IN %4 AND %5 "
                          "ORDER BY GenderIndex, ClassID DESC")
                          .arg(charInfos.raceID).arg(charInfos.sexID).arg(GENDER_ANY).arg(idListStr).arg(classFilter);
  if (queryItemInfo(query, iteminfos))
    return iteminfos.values[0][0].toInt();

  // Failed to find model for that specific race and sex, so check fallback race:
  if (charInfos.textureFallbackRaceID > 0)
  {
    query = QString("SELECT ID FROM ComponentTextureFileData "
                    "WHERE RaceID = %1 AND (GenderIndex = %2 OR GenderIndex = %3) "
                    "AND ID IN %4 AND %5 "
                    "ORDER BY GenderIndex, ClassID DESC")
                    .arg(charInfos.textureFallbackRaceID).arg(charInfos.textureFallbackSexID).arg(GENDER_ANY).arg(idListStr).arg(classFilter);

    if (queryItemInfo(query, iteminfos))
      return iteminfos.values[0][0].toInt();
  }
  
  // We still didn't find the model, so check for RACE_ANY (race = 0) items: 
  query = QString("SELECT ID FROM ComponentTextureFileData "
                  "WHERE RaceID = %1 AND (GenderIndex = %2 OR GenderIndex = %3) "
                  "AND ID IN %4 AND %5 "
                  "ORDER BY GenderIndex, ClassID DESC")
                  .arg(RACE_ANY).arg(charInfos.sexID).arg(GENDER_ANY).arg(idListStr).arg(classFilter);

  if (queryItemInfo(query, iteminfos))
    return iteminfos.values[0][0].toInt();

  return 0;
}
