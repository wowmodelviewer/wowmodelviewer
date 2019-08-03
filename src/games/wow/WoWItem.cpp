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

map<CharSlots, int> WoWItem::SLOT_LAYERS = { { CS_SHIRT, 10 }, { CS_HEAD, 11 }, { CS_SHOULDER, 13 },
                                             { CS_PANTS, 10 }, { CS_BOOTS, 11 }, { CS_CHEST, 13 },
                                             { CS_TABARD, 17 }, { CS_BELT, 18 }, { CS_BRACERS, 19 },
                                             { CS_GLOVES, 20 }, { CS_HAND_RIGHT, 21 }, { CS_HAND_LEFT, 22 },
                                             { CS_CAPE, 23 }, { CS_QUIVER, 24 } };


WoWItem::WoWItem(CharSlots slot)
  : m_charModel(nullptr), m_id(-1), m_displayId(-1),
  m_quality(0), m_level(0), m_type(0),
  m_nbLevels(0), m_slot(slot), m_mergedModel(nullptr)
{
  setName("---- None ----");
}

void WoWItem::setId(int id)
{
  if (id != m_id)
  {
    m_id = id;

    if (m_id == 0)
    {
      unload();
      // reset name and quality
      setName("---- None ----");
      m_quality = 0;
      m_type = 0;

      if (m_slot == CS_HAND_RIGHT)
        m_charModel->charModelDetails.closeRHand = false;

      if (m_slot == CS_HAND_LEFT)
        m_charModel->charModelDetails.closeLHand = false;

      return;
    }

    auto itemlevels = GAMEDATABASE.sqlQueryAssoc(QString("SELECT ItemLevel, ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %1").arg(id));

    if (!itemlevels.empty())
    {
      m_nbLevels = 0;
      m_level = 0;
      m_levelDisplayMap.clear();
      for (auto &it : itemlevels.values)
      {
        const auto curid = it["ItemAppearanceID"].toInt();

        // if display id is null (case when item's look doesn't change with level)
        if (curid == 0)
          continue;

        //check if display id already in the map (do not duplicate when look is the same)
        auto found = false;
        for (auto &lvl : m_levelDisplayMap)
        {
          if (lvl.second == curid)
          {
            found = true;
            break;
          }
        }

        if (!found)
        {
          m_levelDisplayMap[m_nbLevels] = curid;
          m_nbLevels++;
        }
      }
    }

    auto iteminfos = GAMEDATABASE.sqlQueryAssoc(QString("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = %1").arg(m_levelDisplayMap[m_level]));

    if (!iteminfos.empty())
      m_displayId = iteminfos.values[0]["ItemDisplayInfoID"].toInt();

    const auto itemRcd = items.getById(id);
    setName(itemRcd.name);
    m_quality = itemRcd.quality;
    m_type = itemRcd.type;
    load();
  }
}

void WoWItem::setDisplayId(int id)
{
  if (m_displayId != id)
  {
    m_id = -1;
    m_displayId = id; // to update from database;
    setName("NPC Item");
    load();
  }
}

void WoWItem::setLevel(int level)
{
  if ((m_nbLevels > 1) && (m_level != level))
  {
    m_level = level;

    auto iteminfos = GAMEDATABASE.sqlQueryAssoc(QString("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = %1").arg(m_levelDisplayMap[m_level]));

    if (!iteminfos.empty())
      m_displayId = iteminfos.values[0]["ItemDisplayInfoID"].toInt();

    const auto itemRcd = items.getById(m_id);
    setName(itemRcd.name);
    m_quality = itemRcd.quality;
    m_type = itemRcd.type;
    load();
  }
}


void WoWItem::onParentSet(Component * parent)
{
  m_charModel = dynamic_cast<WoWModel *>(parent);
}

void WoWItem::unload()
{
  // delete models and clear map
  for (auto& m_itemModel : m_itemModels)
  {
    delete m_itemModel.second;
  }
  m_itemModels.clear();

  // release textures and clear map
  for (auto& m_itemTexture : m_itemTextures)
  {
    TEXTUREMANAGER.delbyname(m_itemTexture.second->fullname());
  }
  m_itemTextures.clear();

  // clear map
  m_itemGeosets.clear();

  // remove any existing attachement
  if (m_charModel->attachment)
    m_charModel->attachment->delSlot(m_slot);

  // unload any merged model
  if (m_mergedModel != nullptr)
  {
    // TODO : unmerge trigs refreshMerging that trigs refresh... so m_mergedModel must be null...
    // need to find a better way to solve this
    const auto m = m_mergedModel;
    m_mergedModel = nullptr;
    m_charModel->unmergeModel(m);
    delete m_mergedModel;
  }
}

void WoWItem::load()
{
  unload();

  if (!m_charModel) // no parent => give up
    return;

  if (m_id == 0 || m_displayId == 0) // no equipment, just return
    return;

  RaceInfos charInfos;
  RaceInfos::getCurrent(m_charModel, charInfos);
  sqlResult iteminfos;

  // query geosets infos
  if (!queryItemInfo(QString("SELECT GeoSetGroup1, GeoSetGroup2, GeoSetGroup3, GeoSetGroup4, GeoSetGroup5, GeoSetGroup6 "
                             "FROM ItemDisplayInfo WHERE ItemDisplayInfo.ID = %1").arg(m_displayId), 
                     iteminfos))
    return;

  int geosetGroup[6] = { iteminfos.values[0][0].toInt(), iteminfos.values[0][1].toInt() ,
                         iteminfos.values[0][2].toInt(), iteminfos.values[0][3].toInt() ,
                         iteminfos.values[0][5].toInt(), iteminfos.values[0][5].toInt()};

  // query models
  int model[2] = { getCustomModelId(0), getCustomModelId(1) };

  // query textures
  int texture[2] = { getCustomTextureId(0), getCustomTextureId(1) };

  // query textures from ItemDisplayInfoMaterialRes (if relevant)
  const auto texinfos = GAMEDATABASE.sqlQuery(QString("SELECT * FROM ItemDisplayInfoMaterialRes WHERE ItemDisplayInfoID = %1").arg(m_displayId));
  if (texinfos.valid && !texinfos.empty())
  {
    if (queryItemInfo(QString("SELECT TextureID FROM ItemDisplayInfoMaterialRes "
                              "LEFT JOIN TextureFileData ON TextureFileDataID = TextureFileData.ID "
                              "INNER JOIN ComponentTextureFileData ON ComponentTextureFileData.ID = TextureFileData.TextureID "
                              "AND (ComponentTextureFileData.GenderIndex = 3 OR ComponentTextureFileData.GenderIndex = %1) "
                              "WHERE ItemDisplayInfoID = %2").arg(charInfos.sexid).arg(m_displayId),
                      iteminfos))
    {
      for (uint i = 0; i < iteminfos.values.size(); i++)
      {
        GameFile * tex = GAMEDIRECTORY.getFile(iteminfos.values[i][0].toInt());
        if (tex)
        {
          TEXTUREMANAGER.add(tex);
          m_itemTextures[getRegionForTexture(tex)] = tex;
        }
      }
    }
  }

  switch (m_slot)
  {
    case CS_HEAD:
    {
      // attachements
      updateItemModel(ATT_HELMET, model[0], texture[0]);

      // geosets
      // Head: {geosetGroup[0] = 2700**, geosetGroup[1] = 2101 }
      m_itemGeosets[CG_GEOSET2700] = 1 + geosetGroup[0];
      m_itemGeosets[CG_GEOSET2100] = 1 + geosetGroup[1];
      break;
    }
    case CS_SHOULDER:
    {
      //geosets
      // Shoulder: {geosetGroup[0] = 2601}
      m_itemGeosets[CG_GEOSET2600] = 1 + geosetGroup[0];

      // find position index value from ComponentModelFileData table
      const auto query = QString("SELECT ID, PositionIndex FROM ComponentModelFileData "
                                 "WHERE ID IN (%1,%2)").arg(model[0]).arg(model[1]);
      auto result = GAMEDATABASE.sqlQueryAssoc(query);

      auto leftIndex = 0;
      auto rightIndex = 1;
      if (!result.empty())
      {
        const auto modelid = result.values[0]["ID"].toInt();
        const auto position = result.values[0]["PositionIndex"].toInt();
        
        if (modelid == model[0])
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
        LOG_ERROR << "Impossible to query information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ") - SQL ERROR";
        LOG_ERROR << query;
      }

      LOG_INFO << "leftIndex" << leftIndex << "rightIndex" << rightIndex;

      // left shoulder
      updateItemModel(ATT_LEFT_SHOULDER, model[leftIndex], texture[leftIndex]);

      // right shoulder
      updateItemModel(ATT_RIGHT_SHOULDER, model[rightIndex], texture[rightIndex]);
      
      break;
    }
    case CS_BOOTS:
    {
      // geosets
      // Boots: {geosetGroup[0] = 501, geosetGroup[1] = 2000*}
      m_itemGeosets[CG_BOOTS] = 1 + geosetGroup[0];
      m_itemGeosets[CG_HDFEET] = 1 + geosetGroup[1];

      // models
      mergeModel(CS_BOOTS, model[0], texture[0]);

      break;
    }
    case CS_BELT:
    {
 
      // geosets
      // Waist: {geosetGroup[0] = 1801}
      m_itemGeosets[CG_BELT] = 1 + geosetGroup[0];

      // buckle model
      updateItemModel(ATT_BELT_BUCKLE, model[0], texture[0]);
      
      // belt model
      mergeModel(CS_BELT, model[1], texture[1]);
     
      break;
    }
    case CS_PANTS:
    {
      // geosets
      // Pants: {geosetGroup[0] = 1101, geosetGroup[1] = 901, geosetGroup[2] = 1301}
      m_itemGeosets[CG_PANTS2] = 1 + geosetGroup[0];
      m_itemGeosets[CG_KNEEPADS] = 1 + geosetGroup[1];
      m_itemGeosets[CG_TROUSERS] = 1 + geosetGroup[2];

     
      //model
      mergeModel(CS_PANTS, model[0], texture[0]);

      break;
    }
    case CS_SHIRT:
    case CS_CHEST:
    {
      // geosets
      // Chest: {geosetGroup[0] = 801, geosetGroup[1] = 1001, geosetGroup[2] = 1301, geosetGroup[3] = 2201, geosetGroup[4] = 2801}
      m_itemGeosets[CG_WRISTBANDS] = 1 + geosetGroup[0];
      m_itemGeosets[CG_PANTS] = 1 + geosetGroup[1];
      m_itemGeosets[CG_TROUSERS] = 1 + geosetGroup[2];
      m_itemGeosets[CG_GEOSET2200] = 1 + geosetGroup[3];
      m_itemGeosets[CG_GEOSET2800] = 1 + geosetGroup[4];

      // model
      mergeModel(CS_CHEST, model[0], texture[0]);

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
      m_itemGeosets[CG_GLOVES] = 1 + geosetGroup[0];
      m_itemGeosets[CG_HANDS] = 1 + geosetGroup[1];

      // model
      mergeModel(CS_GLOVES, model[0], texture[0]);
    
      break;
    }
    case CS_HAND_RIGHT:
    case CS_HAND_LEFT:
    {
      updateItemModel(((m_slot == CS_HAND_RIGHT) ? ATT_RIGHT_PALM : ATT_LEFT_PALM), model[0], texture[0]);
      break;
    }
    case CS_CAPE:
    {
      GameFile * tex = GAMEDIRECTORY.getFile(texture[0]);
      if (tex)
      {
        TEXTUREMANAGER.add(tex);
        m_itemTextures[getRegionForTexture(tex)] = tex;
      }

      // Cape: {geosetGroup[0] = 1501}
      m_itemGeosets[CG_CAPE] = 1 + geosetGroup[0];

      break;
    }
    case CS_TABARD:
    {
      if (isCustomizableTabard())
      {
        m_charModel->td.showCustom = true;
        m_itemGeosets[CG_TARBARD] = 2;

        auto tex = GAMEDIRECTORY.getFile(m_charModel->td.GetBackgroundTex(CR_TORSO_UPPER));
        if (tex)
        {
          TEXTUREMANAGER.add(tex);
          m_itemTextures[CR_TABARD_1] = tex;
        }

        tex = GAMEDIRECTORY.getFile(m_charModel->td.GetBackgroundTex(CR_TORSO_LOWER));
        if (tex)
        {
          TEXTUREMANAGER.add(tex);
          m_itemTextures[CR_TABARD_2] = tex;
        }

        tex = GAMEDIRECTORY.getFile(m_charModel->td.GetIconTex(CR_TORSO_UPPER));
        if (tex)
        {
          TEXTUREMANAGER.add(tex);
          m_itemTextures[CR_TABARD_3] = tex;
        }

        tex = GAMEDIRECTORY.getFile(m_charModel->td.GetIconTex(CR_TORSO_LOWER));
        if (tex)
        {
          TEXTUREMANAGER.add(tex);
          m_itemTextures[CR_TABARD_4] = tex;
        }

        tex = GAMEDIRECTORY.getFile(m_charModel->td.GetBorderTex(CR_TORSO_UPPER));
        if (tex)
        {
          TEXTUREMANAGER.add(tex);
          m_itemTextures[CR_TABARD_5] = tex;
        }

        tex = GAMEDIRECTORY.getFile(m_charModel->td.GetBorderTex(CR_TORSO_LOWER));
        if (tex)
        {
          TEXTUREMANAGER.add(tex);
          m_itemTextures[CR_TABARD_6] = tex;
        }
      }
      else
      {
        m_charModel->td.showCustom = false;

        // geosets
        // Tabard: {geosetGroup[0] = 1201}
        m_itemGeosets[CG_TARBARD] = 1 + geosetGroup[0];
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
  if (m_id == 0) // no item equipped, give up
    return;

  // merge model if any
  if (m_mergedModel != nullptr)
    m_charModel->mergeModel(m_mergedModel);

  // update geoset values
  for (auto &it : m_itemGeosets)
  {
    if ((m_slot != CS_BOOTS) && // treat boots geoset in a special case - cf CS_BOOTS
        (m_slot != CS_PANTS)) // treat trousers geoset in a special case - cf CS_PANTS
    {
      m_charModel->cd.geosets[it.first] = it.second;

      if (m_mergedModel != nullptr)
        m_mergedModel->setGeosetGroupDisplay(it.first, 1);
    }
  }

  // attach items if any
  if (m_charModel->attachment)
  {
    if ((m_slot != CS_HAND_RIGHT) && // treat right hand attachment in a special case - cf CS_HAND_RIGHT
        (m_slot != CS_HAND_LEFT))    // treat left hand attachment in a special case - cf CS_HAND_LEFT
    {
      m_charModel->attachment->delSlot(m_slot);
      for (auto &it : m_itemModels)
        m_charModel->attachment->addChild(it.second, it.first, m_slot);
    }
  }

  // add textures if any
  if ((m_slot != CS_BOOTS) &&  // treat boots texturing in a special case - cf CS_BOOTS
      (m_slot != CS_GLOVES) && // treat gloves texturing in a special case - cf CS_GLOVES 
      (m_slot != CS_TABARD) && // treat tabard texturing in a special case - cf CS_TABARD 
      (m_slot != CS_CAPE))     // treat cape texturing in a special case - cf CS_CAPE 
  {
    for (auto &it : m_itemTextures)
      m_charModel->tex.addLayer(it.second, it.first, SLOT_LAYERS[m_slot]);
  }
  

  switch (m_slot)
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
      if (m_charModel->attachment)
      {
        m_charModel->attachment->delSlot(CS_HAND_RIGHT);

        const auto it = m_itemModels.find(ATT_RIGHT_PALM);
        if (it != m_itemModels.end())
        {
          int attachement = ATT_RIGHT_PALM;
          const auto& item = items.getById(m_id);
          if (m_charModel->bSheathe &&  item.sheath != SHEATHETYPE_NONE)
          {
            // make the weapon cross
            if (item.sheath == ATT_LEFT_BACK_SHEATH)
              attachement = ATT_RIGHT_BACK_SHEATH;
            if (item.sheath == ATT_LEFT_BACK)
              attachement = ATT_RIGHT_BACK;
            if (item.sheath == ATT_LEFT_HIP_SHEATH)
              attachement = ATT_RIGHT_HIP_SHEATH;
          }

          if (m_charModel->bSheathe)
            m_charModel->charModelDetails.closeRHand = false;
          else
            m_charModel->charModelDetails.closeRHand = true;

          m_charModel->attachment->addChild(it->second, attachement, m_slot);
        }
      }
      break;
    }
    case CS_HAND_LEFT:
    {
      if (m_charModel->attachment)
      {
        m_charModel->attachment->delSlot(CS_HAND_LEFT);

        const auto it = m_itemModels.find(ATT_LEFT_PALM);
        if (it != m_itemModels.end())
        {
          const auto& item = items.getById(m_id);
          int attachement = ATT_LEFT_PALM;

          if (item.type == IT_SHIELD)
            attachement = ATT_LEFT_WRIST;

          if (m_charModel->bSheathe &&  item.sheath != SHEATHETYPE_NONE)
            attachement = static_cast<POSITION_SLOTS>(item.sheath);

          if (m_charModel->bSheathe || item.type == IT_SHIELD)
            m_charModel->charModelDetails.closeLHand = false;
          else
            m_charModel->charModelDetails.closeLHand = true;

          Vec3D rot(0., 0., 0.);

          // if item is a warglaive, mirror it in hand
          if (((item.itemclass == 2) && (item.subclass == 9) && (item.sheath == 27)) ||
              // and same if it's a fist
              ((item.itemclass == 2) && (item.subclass == 13)))
            rot.y = 180.;

          m_charModel->attachment->addChild(it->second, attachement, m_slot, 1., rot);
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
      for (auto &it : m_itemGeosets)
      {
        if (it.first != CG_BOOTS)
        {
          m_charModel->cd.geosets[it.first] = it.second;

          if (m_mergedModel != nullptr)
            m_mergedModel->setGeosetGroupDisplay(it.first, 1);
        }
      }

      const auto geoIt = m_itemGeosets.find(CG_BOOTS);

      if (geoIt != m_itemGeosets.end())
      {
        // don't render boots behind robe
        const auto chestItem = m_charModel->getItem(CS_CHEST);
        if (chestItem->m_type != IT_ROBE) // maybe not handle when geoIt->second = 5 ?
        {
          m_charModel->cd.geosets[CG_BOOTS] = geoIt->second;
          if (m_mergedModel != nullptr)
            m_mergedModel->setGeosetGroupDisplay(CG_BOOTS, 1);
        }
      }

      auto texIt = m_itemTextures.find(CR_LEG_LOWER);
      if (texIt != m_itemTextures.end())
        m_charModel->tex.addLayer(texIt->second, CR_LEG_LOWER, SLOT_LAYERS[m_slot]);

      if (!m_charModel->cd.showFeet)
      {
        texIt = m_itemTextures.find(CR_FOOT);
        if (texIt != m_itemTextures.end())
          m_charModel->tex.addLayer(texIt->second, CR_FOOT, SLOT_LAYERS[m_slot]);
      }
      break;
    }
    case CS_PANTS:
    {
      for (auto &it : m_itemGeosets)
      {
        if (it.first != CG_TROUSERS)
        {
          m_charModel->cd.geosets[it.first] = it.second;

          if (m_mergedModel != nullptr)
            m_mergedModel->setGeosetGroupDisplay(it.first, 1);
        }
      }

      const auto geoIt = m_itemGeosets.find(CG_TROUSERS);

      if (geoIt != m_itemGeosets.end())
      {
        // apply trousers geosets only if character is not already wearing a robe
        const auto& item = items.getById(m_charModel->getItem(CS_CHEST)->id());

        if (item.type != IT_ROBE)
        {
          m_charModel->cd.geosets[CG_TROUSERS] = geoIt->second;
          if (m_mergedModel)
            m_mergedModel->setGeosetGroupDisplay(CG_TROUSERS, 1);
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
      auto texIt = m_itemTextures.find(CR_ARM_LOWER);

      auto layer = SLOT_LAYERS[m_slot];

      // if we are wearing a robe, render gloves first in texture compositing
      // only if GeoSetGroup1 is 0 (from item displayInfo db) which corresponds to stored geoset equals to 1
      const auto chestItem = m_charModel->getItem(CS_CHEST);
      if ((chestItem->m_type == IT_ROBE) && (m_charModel->cd.geosets[CG_GLOVES] == 1))
        layer = SLOT_LAYERS[CS_CHEST] - 1;

      if (texIt != m_itemTextures.end())
        m_charModel->tex.addLayer(texIt->second, CR_ARM_LOWER, layer);

      texIt = m_itemTextures.find(CR_HAND);
      if (texIt != m_itemTextures.end())
        m_charModel->tex.addLayer(texIt->second, CR_HAND, layer);
      break;
    }
    case CS_CAPE:
    {
      const auto it = m_itemTextures.find(CR_CAPE);
      if (it != m_itemTextures.end())
        m_charModel->updateTextureList(it->second, TEXTURE_CAPE);
      break;
    }
    case CS_TABARD:
    {
      if (isCustomizableTabard())
      {
        auto it = m_itemTextures.find(CR_TABARD_1);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

        it = m_itemTextures.find(CR_TABARD_2);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);

        it = m_itemTextures.find(CR_TABARD_3);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

        it = m_itemTextures.find(CR_TABARD_4);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);

        it = m_itemTextures.find(CR_TABARD_5);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

        it = m_itemTextures.find(CR_TABARD_6);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);

      }
      else
      {
        auto it = m_itemTextures.find(CR_TORSO_UPPER);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

        it = m_itemTextures.find(CR_TORSO_LOWER);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);
      }
      break;
    }
    default:
      break;
  }
}

bool WoWItem::isCustomizableTabard() const
{
  return (m_id == 5976 || // Guild Tabard
          m_id == 69209 || // Illustrious Guild Tabard
          m_id == 69210);  // Renowned Guild Tabard
}

void WoWItem::save(QXmlStreamWriter & stream) const
{
  stream.writeStartElement("item");

  stream.writeStartElement("slot");
  stream.writeAttribute("value", QString::number(m_slot));
  stream.writeEndElement();

  stream.writeStartElement("id");
  stream.writeAttribute("value", QString::number(m_id));
  stream.writeEndElement();

  stream.writeStartElement("displayId");
  stream.writeAttribute("value", QString::number(m_displayId));
  stream.writeEndElement();

  stream.writeStartElement("level");
  stream.writeAttribute("value", QString::number(m_level));
  stream.writeEndElement();

  if (isCustomizableTabard())
    m_charModel->td.save(stream);

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

        if (slot == m_slot)
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
                if (m_id == -1)
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
    while (!reader.isStartElement())
      reader.readNext();

    if (reader.name() == "TabardDetails")
    {
      m_charModel->td.load(reader);
      load(); // refresh tabard textures
    }
  }
}

void WoWItem::updateItemModel(POSITION_SLOTS pos, int modelId, int textureId)
{
  LOG_INFO << __FUNCTION__ << pos << modelId << textureId;

  if (modelId == 0)
    return;

  auto m = new WoWModel(GAMEDIRECTORY.getFile(modelId), true);

  if (m->ok)
  {
    for (uint i = 0; i < m->geosets.size(); i++)
      m->showGeoset(i, true);

    m_itemModels[pos] = m;
    const auto texture = GAMEDIRECTORY.getFile(textureId);
    if (texture)
      m->updateTextureList(texture, TEXTURE_ITEM);
    else
      LOG_ERROR << "Error during item update" << m_id << "(display id" << m_displayId << "). Texture" << textureId << "can't be loaded";
  }
  else
  {
    LOG_ERROR << "Error during item update" << m_id << "(display id" << m_displayId << "). Model" << modelId << "can't be loaded";
  }
}

void WoWItem::mergeModel(CharSlots slot, int modelId, int textureId)
{
  if (modelId == 0)
    return;

  m_mergedModel = new WoWModel(GAMEDIRECTORY.getFile(modelId), true);

  if (m_mergedModel->ok)
  {
    GameFile * texture = GAMEDIRECTORY.getFile(textureId);
    if (texture)
      m_mergedModel->updateTextureList(texture, TEXTURE_ITEM);
    else
      LOG_ERROR << "Error during item update" << m_id << "(display id" << m_displayId << "). Texture" << textureId << "can't be loaded";

    for (uint i = 0; i < m_mergedModel->geosets.size(); i++)
      m_mergedModel->showGeoset(i, false);
  }
  else
  {
    LOG_ERROR << "Error during item update" << m_id << "(display id" << m_displayId << "). Model" << modelId << "can't be loaded";
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
      LOG_ERROR << "Unable to determine region for texture" << fullname << " - item" << m_id << "displayid" << m_displayId;
    }
  }

  return result;
}

bool WoWItem::queryItemInfo(QString & query, sqlResult & result) const
{
  result = GAMEDATABASE.sqlQuery(query);
 
  if (!result.valid || result.values.empty())
  {
    LOG_ERROR << "Impossible to query information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ") - SQL ERROR";
    LOG_ERROR << query;
    return false;
  }

  return true;
}

int WoWItem::getCustomModelId(size_t index) const
{
  sqlResult infos;
  if (!queryItemInfo(QString("SELECT ModelID FROM ItemDisplayInfo "
                             "LEFT JOIN ModelFileData ON %1 = ModelFileData.ID "
                             "WHERE ItemDisplayInfo.ID = %2").arg((index == 0)?"Model1":"Model2").arg(m_displayId),
                     infos))
    return 0;

  // if there is only one result, return directly model id
  if (infos.values.size() == 1)
    return infos.values[0][0].toInt();

  // if there are multiple values, filter them based on ComponentModelFileData table
  std::vector<QString> ids;
  for (auto& value : infos.values)
    ids.push_back(value[0]);

  QString query = "SELECT ID, GenderIndex, classID, RaceID ";
  query += "FROM ComponentModelFileData WHERE ID IN(";
  for (size_t i = 0; i < ids.size() - 1; i++)
  {
    query += ids[i];
    query += ",";
  }
  query += ids[ids.size() - 1];
  query += ")";

  sqlResult iteminfos;
  if (queryItemInfo(query, iteminfos))
  {
    RaceInfos charInfos;
    RaceInfos::getCurrent(m_charModel, charInfos);

    size_t i = 0;
    for (auto it : iteminfos.values)
    {
      const auto gender = it[1].toInt();
      const auto race = it[3].toInt();
      // models are customized by race and gender
      // if gender == 2, no customization
      auto fallbackRaceID = 0;
      if (gender == 0)
        fallbackRaceID = charInfos.MaleModelFallbackRaceID;
      else if (gender == 1)
        fallbackRaceID = charInfos.FemaleModelFallbackRaceID;
      if ((gender == charInfos.sexid) && ((race == charInfos.raceid) || (fallbackRaceID > 0 && (race == fallbackRaceID))))
        return it[0].toInt();
      else if ((gender == 2) && (i == index))
        return it[0].toInt();
      i++;
    }
  }

  return 0;
}

int WoWItem::getCustomTextureId(size_t index) const
{
  sqlResult infos;
  if (!queryItemInfo(QString("SELECT TextureID FROM ItemDisplayInfo "
                             "LEFT JOIN TextureFileData ON %1 = TextureFileData.ID "
                             "WHERE ItemDisplayInfo.ID = %2").arg((index == 0) ? "TextureItemID1" : "TextureItemID2").arg(m_displayId),
                     infos))
    return 0;

  // if there is only one result, return directly texture id
  if (infos.values.size() == 1)
    return infos.values[0][0].toInt();

  // if there are multiple values, filter them based on ComponentTextureFileData table
  std::vector<QString> ids;
  for (auto& value : infos.values)
    ids.push_back(value[0]);

  QString query = "SELECT ID, GenderIndex, classID, RaceID ";
  query += "FROM ComponentTextureFileData WHERE ID IN(";
  for (size_t i = 0; i < ids.size() - 1; i++)
  {
    query += ids[i];
    query += ",";
  }
  query += ids[ids.size() - 1];
  query += ")";

  sqlResult iteminfos;
  if (queryItemInfo(query, iteminfos))
  {
    RaceInfos charInfos;
    RaceInfos::getCurrent(m_charModel, charInfos);

    for (auto it : iteminfos.values)
    {
      const auto gender = it[1].toInt();
      const auto race = it[3].toInt();
      auto fallbackRaceID = 0;
      if (gender == 0)
        fallbackRaceID = charInfos.MaleTextureFallbackRaceID;
      else if (gender == 1)
        fallbackRaceID = charInfos.FemaleTextureFallbackRaceID;
      // models are customized by race and gender (gender == 3 means both sex)
      if (((gender == charInfos.sexid) || (gender == 3)) && ((race == charInfos.raceid) || (fallbackRaceID > 0 && (race == fallbackRaceID))))
        return it[0].toInt();
    }
  }

  return 0;
}
