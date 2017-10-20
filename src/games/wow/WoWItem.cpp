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
#include <QXmlStreamReader>
#include <QXmlStreamWriter>


#include "Attachment.h"
#include "database.h" // items
#include "Game.h"
#include "globalvars.h"
#include "RaceInfos.h"
#include "wow_enums.h"
#include "WoWDatabase.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#define DEBUG_FILTERING 1

map<CharSlots, int> WoWItem::SLOT_LAYERS = { { CS_SHIRT, 10 }, { CS_HEAD, 11 }, { CS_SHOULDER, 13 },
                                             { CS_PANTS, 10 }, { CS_BOOTS, 11 }, { CS_CHEST, 13 },
                                             { CS_TABARD, 17 }, { CS_BELT, 18 }, { CS_BRACERS, 19 },
                                             { CS_GLOVES, 20 }, { CS_HAND_RIGHT, 21 }, { CS_HAND_LEFT, 22 },
                                             { CS_CAPE, 23 }, { CS_QUIVER, 24 } };


WoWItem::WoWItem(CharSlots slot)
  : m_charModel(0), m_id(-1), m_quality(0),
  m_slot(slot), m_displayId(-1), m_level(0),
  m_nbLevels(0), m_type(0), m_mergedModel(0)
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

    QString query = QString("SELECT ItemLevel, ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %1").arg(id);
    sqlResult itemlevels = GAMEDATABASE.sqlQuery(query);

    if (itemlevels.valid && !itemlevels.values.empty())
    {
      m_nbLevels = 0;
      m_level = 0;
      m_levelDisplayMap.clear();
      for (unsigned int i = 0; i < itemlevels.values.size(); i++)
      {
        int curid = itemlevels.values[i][1].toInt();

        // if display id is null (case when item's look doesn't change with level)
        if (curid == 0)
          continue;

        //check if display id already in the map (do not duplicate when look is the same)
        bool found = false;
        for (std::map<int, int>::iterator it = m_levelDisplayMap.begin(); it != m_levelDisplayMap.end(); ++it)
        {
          if (it->second == curid)
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

    query = QString("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = %1")
      .arg(m_levelDisplayMap[m_level]);

    sqlResult iteminfos = GAMEDATABASE.sqlQuery(query);

    if (iteminfos.valid && !iteminfos.values.empty())
      m_displayId = iteminfos.values[0][0].toInt();

    ItemRecord itemRcd = items.getById(id);
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

void WoWItem::setLevel(unsigned int level)
{
  if ((m_nbLevels > 1) && (m_level != level))
  {
    m_level = level;

    QString query = QString("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = %1")
      .arg(m_levelDisplayMap[m_level]);

    sqlResult iteminfos = GAMEDATABASE.sqlQuery(query);

    if (iteminfos.valid && !iteminfos.values.empty())
      m_displayId = iteminfos.values[0][0].toInt();

    ItemRecord itemRcd = items.getById(m_id);
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
  for (std::map<POSITION_SLOTS, WoWModel *>::iterator it = m_itemModels.begin(),
       itEnd = m_itemModels.end();
       it != itEnd;
  ++it)
  {
    delete it->second;
  }
  m_itemModels.clear();

  // release textures and clear map
  for (std::map<CharRegions, GameFile *>::iterator it = m_itemTextures.begin(),
       itEnd = m_itemTextures.end();
       it != itEnd;
  ++it)
  {
    TEXTUREMANAGER.delbyname(it->second->fullname());
  }
  m_itemTextures.clear();

  // clear map
  m_itemGeosets.clear();

  // remove any existing attachement
  if (m_charModel->attachment)
    m_charModel->attachment->delSlot(m_slot);

  // unload any merged model
  if (m_mergedModel != 0)
  {
    // TODO : unmerge trigs refreshMerging that trigs refresh... so m_mergedModel must be null...
    // need to find a better way to solve this
    WoWModel * m = m_mergedModel;
    m_mergedModel = 0;
    m_charModel->unmergeModel(m);
    delete m_mergedModel;
  }
}

void WoWItem::load()
{
  unload();

  if (!m_charModel) // no parent => give up
    return;

  if (m_id == 0) // no equipment, just return
    return;

  switch (m_slot)
  {
    case CS_HEAD:
    {
      QString query = QString("SELECT ModelID, TextureID FROM ItemDisplayInfo "
                              "LEFT JOIN ModelFileData ON Model1 = ModelFileData.ID "
                              "LEFT JOIN TextureFileData ON TextureItemID1 = TextureFileData.ID "
                              "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);

      sqlResult iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), MODEL, 0);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ") - SQL ERROR";
        return;
      }

      updateItemModel(ATT_HELMET, iteminfos.values[0][0].toInt(), iteminfos.values[0][1].toInt());

      break;
    }
    case CS_SHOULDER:
    {
      QString query = QString("SELECT Model1, TextureItemID1, Model2, TextureItemID2 FROM ItemDisplayInfo "
                              "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);


      sqlResult iteminfos = GAMEDATABASE.sqlQuery(query);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      if ((iteminfos.values[0][0].toInt() != 0) && (iteminfos.values[0][2].toInt() != 0)) // both shoulders
      {
        sqlResult modelinfos = GAMEDATABASE.sqlQuery(QString("SELECT ModelID FROM ModelFileData WHERE ID = %1").arg(iteminfos.values[0][0]));
        sqlResult texinfos = GAMEDATABASE.sqlQuery(QString("SELECT ID, TextureID FROM TextureFileData WHERE ID IN( %1, %2)").arg(iteminfos.values[0][1]).arg(iteminfos.values[0][3]));

        if (!modelinfos.valid || modelinfos.values.empty() || !texinfos.valid || texinfos.values.empty())
        {
          LOG_ERROR << "Impossible to query model & texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
          return;
        }

        // associate left / right model infos
        GameFile * file = GAMEDIRECTORY.getFile(modelinfos.values[0][0].toInt());
        int leftmodelindex = (file->fullname().contains("lshoulder", Qt::CaseInsensitive) || file->fullname().endsWith("_l.m2", Qt::CaseInsensitive)) ? 0 : 1;
        int rightmodelindex = leftmodelindex ? 0 : 1;

        // create texture map to use result correctly (especially when left texture is diffrent from right texture)
        std::map<QString, int> textures;
        for (uint i = 0; i < texinfos.values.size(); i++)
          textures[texinfos.values[i][0]] = texinfos.values[i][1].toInt();

        // left shoulder
        updateItemModel(ATT_LEFT_SHOULDER, modelinfos.values[leftmodelindex][0].toInt(), textures[iteminfos.values[0][1]]);

        // right shoulder
        updateItemModel(ATT_RIGHT_SHOULDER, modelinfos.values[rightmodelindex][0].toInt(), textures[iteminfos.values[0][3]]);
      }
      else if (iteminfos.values[0][2].toInt() == 0) // only left shoulder
      {
        sqlResult modelinfos = GAMEDATABASE.sqlQuery(QString("SELECT ModelID FROM ModelFileData WHERE ID = %1").arg(iteminfos.values[0][0]));
        sqlResult texinfos = GAMEDATABASE.sqlQuery(QString("SELECT TextureID FROM TextureFileData WHERE ID = %1").arg(iteminfos.values[0][1]));

        if (!modelinfos.valid || modelinfos.values.empty() || !texinfos.valid || texinfos.values.empty())
        {
          LOG_ERROR << "Impossible to query model & texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
          return;
        }

        int leftmodelindex = -1;

        for (uint i = 0; i < modelinfos.values.size(); i++)
        {
          GameFile * file = GAMEDIRECTORY.getFile(modelinfos.values[i][0].toInt());
          if (file)
          {
            if (file->fullname().contains("lshoulder", Qt::CaseInsensitive))
              leftmodelindex = i;
          }
        }

        updateItemModel(ATT_LEFT_SHOULDER, modelinfos.values[leftmodelindex][0].toInt(), texinfos.values[0][0].toInt());
      }
      else if (iteminfos.values[0][0].toInt() == 0) // only right shoulder 
      {
        sqlResult modelinfos = GAMEDATABASE.sqlQuery(QString("SELECT ModelID FROM ModelFileData WHERE ID = %1").arg(iteminfos.values[0][2]));
        sqlResult texinfos = GAMEDATABASE.sqlQuery(QString("SELECT TextureID FROM TextureFileData WHERE ID = %1").arg(iteminfos.values[0][3]));

        if (!modelinfos.valid || modelinfos.values.empty() || !texinfos.valid || texinfos.values.empty())
        {
          LOG_ERROR << "Impossible to query model & texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
          return;
        }

        int rightmodelindex = -1;

        for (uint i = 0; i < modelinfos.values.size(); i++)
        {
          GameFile * file = GAMEDIRECTORY.getFile(modelinfos.values[i][0].toInt());
          if (file)
          {
            if (file->fullname().contains("rshoulder", Qt::CaseInsensitive))
              rightmodelindex = i;
          }
        }

        if (rightmodelindex != -1)
          updateItemModel(ATT_RIGHT_SHOULDER, modelinfos.values[rightmodelindex][0].toInt(), texinfos.values[0][0].toInt());
      }
      break;
    }
    case CS_BOOTS:
    {
      // query texture infos from ItemDisplayInfoMaterialRes
      QString query = QString("SELECT TextureID FROM ItemDisplayInfoMaterialRes "
                              "LEFT JOIN TextureFileData ON TextureFileDataID = TextureFileData.ID "
                              "WHERE ItemDisplayInfoID = %1").arg(m_displayId);

      sqlResult iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), TEXTURE, 0);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      for (uint i = 0; i < iteminfos.values.size(); i++)
      {
        GameFile * texture = GAMEDIRECTORY.getFile(iteminfos.values[i][0].toInt());
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[getRegionForTexture(texture)] = texture;
        }
      }

      // now get geoset / model infos
      query = QString("SELECT ModelID, TextureID, GeoSetGroup1 FROM ItemDisplayInfo "
                      "LEFT JOIN ModelFileData ON Model1 = ModelFileData.ID "
                      "LEFT JOIN TextureFileData ON TextureItemID1 = TextureFileData.ID "
                      "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);

      iteminfos = GAMEDATABASE.sqlQuery(query);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query geoset/model information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      // geoset
      m_itemGeosets[CG_BOOTS] = 1 + iteminfos.values[0][2].toInt();

      // models
      iteminfos = filterSQLResultForModel(iteminfos, MERGED_MODEL, 0);
      if (iteminfos.values[0][0].toInt() != 0) // we have a model for boots
        mergeModel(CS_BOOTS, iteminfos.values[0][0].toInt(), iteminfos.values[0][1].toInt());

      break;
    }
    case CS_BELT:
    {
      // query texture infos from ItemDisplayInfoMaterialRes
      QString query = QString("SELECT TextureID FROM ItemDisplayInfoMaterialRes "
                              "LEFT JOIN TextureFileData ON TextureFileDataID = TextureFileData.ID "
                              "WHERE ItemDisplayInfoID = %1").arg(m_displayId);

      sqlResult iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), TEXTURE, 0);

      if (!iteminfos.valid /* || iteminfos.values.empty() */) // some belts have no texture, only model
      {
        LOG_ERROR << "Impossible to query texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      for (uint i = 0; i < iteminfos.values.size(); i++)
      {
        GameFile * texture = GAMEDIRECTORY.getFile(iteminfos.values[i][0].toInt());
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[getRegionForTexture(texture)] = texture;
        }
      }

      // now get geoset / model infos
      query = QString("SELECT MFD1.ModelID, TFD1.TextureID, MFD2.ModelID, TFD2.TextureID FROM ItemDisplayInfo "
                      "LEFT JOIN ModelFileData AS MFD1 ON Model1 = MFD1.ID "
                      "LEFT JOIN TextureFileData AS TFD1 ON TextureItemID1 = TFD1.ID "
                      "LEFT JOIN ModelFileData AS MFD2 ON Model2 = MFD2.ID "
                      "LEFT JOIN TextureFileData AS TFD2 ON TextureItemID2 = TFD2.ID "
                      "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);

      iteminfos = GAMEDATABASE.sqlQuery(query);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query geoset/model information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      // models
      if (iteminfos.values[0][0].toInt() != 0) // we have a model for belt
      {
        updateItemModel(ATT_BELT_BUCKLE, iteminfos.values[0][0].toInt(), iteminfos.values[0][1].toInt());
      }
      else if (iteminfos.values[0][2].toInt() != 0)
      {
        iteminfos = filterSQLResultForModel(iteminfos, MERGED_MODEL, 2);
        mergeModel(CS_BELT, iteminfos.values[0][2].toInt(), iteminfos.values[0][3].toInt());
      }

      break;
    }
    case CS_PANTS:
    {
      // query texture infos from ItemDisplayInfoMaterialRes
      QString query = QString("SELECT TextureID FROM ItemDisplayInfoMaterialRes "
                              "LEFT JOIN TextureFileData ON TextureFileDataID = TextureFileData.ID "
                              "WHERE ItemDisplayInfoID = %1").arg(m_displayId);

      sqlResult iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), TEXTURE, 0);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      for (uint i = 0; i < iteminfos.values.size(); i++)
      {
        GameFile * texture = GAMEDIRECTORY.getFile(iteminfos.values[i][0].toInt());
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[getRegionForTexture(texture)] = texture;
        }
      }

      // geosets / models
      query = QString("SELECT GeosetGroup2, GeosetGroup3, ModelID, TextureID FROM ItemDisplayInfo "
        "LEFT JOIN ModelFileData ON Model1 = ModelFileData.ID "
        "LEFT JOIN TextureFileData ON TextureItemID1 = TextureFileData.ID "
        "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);

      iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), MERGED_MODEL, 2);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query geoset/model information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      m_itemGeosets[CG_KNEEPADS] = 1 + iteminfos.values[0][0].toInt();
      m_itemGeosets[CG_TROUSERS] = 1 + iteminfos.values[0][1].toInt();

      if (iteminfos.values[0][2].toInt() != 0)
        mergeModel(CS_PANTS, iteminfos.values[0][2].toInt(), iteminfos.values[0][3].toInt());

      break;
    }
    case CS_SHIRT:
    case CS_CHEST:
    {
      // query texture infos from ItemDisplayInfoMaterialRes
      QString query = QString("SELECT TextureID FROM ItemDisplayInfoMaterialRes "
                              "LEFT JOIN TextureFileData ON TextureFileDataID = TextureFileData.ID "
                              "WHERE ItemDisplayInfoID = %1").arg(m_displayId);

      sqlResult iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), TEXTURE, 0);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      for (uint i = 0; i < iteminfos.values.size(); i++)
      {
        GameFile * texture = GAMEDIRECTORY.getFile(iteminfos.values[i][0].toInt());
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[getRegionForTexture(texture)] = texture;
        }
      }

      // geosets
      query = QString("SELECT GeosetGroup1, GeosetGroup2, GeosetGroup3, ModelID, TextureID FROM ItemDisplayInfo "
                      "LEFT JOIN ModelFileData ON Model1 = ModelFileData.ID "
                      "LEFT JOIN TextureFileData ON TextureItemID1 = TextureFileData.ID "
                      "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);

      LOG_INFO << query;

      iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), MERGED_MODEL, 3);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query geoset/model information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      m_itemGeosets[CG_WRISTBANDS] = 1 + iteminfos.values[0][0].toInt();
      m_itemGeosets[CG_TROUSERS] = 1 + iteminfos.values[0][2].toInt();

      if (iteminfos.values[0][3].toInt() != 0)
        mergeModel(CS_CHEST, iteminfos.values[0][3].toInt(), iteminfos.values[0][4].toInt());

      break;
    }
    case CS_BRACERS:
    {
      // query texture infos from ItemDisplayInfoMaterialRes
      QString query = QString("SELECT TextureID FROM ItemDisplayInfoMaterialRes "
                              "LEFT JOIN TextureFileData ON TextureFileDataID = TextureFileData.ID "
                              "WHERE ItemDisplayInfoID = %1").arg(m_displayId);

      sqlResult iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), TEXTURE, 0);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      for (uint i = 0; i < iteminfos.values.size(); i++)
      {
        GameFile * texture = GAMEDIRECTORY.getFile(iteminfos.values[i][0].toInt());
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[getRegionForTexture(texture)] = texture;
        }
      }
      break;
    }
    case CS_GLOVES:
    {
      // query texture infos from ItemDisplayInfoMaterialRes
      QString query = QString("SELECT TextureID FROM ItemDisplayInfoMaterialRes "
                              "LEFT JOIN TextureFileData ON TextureFileDataID = TextureFileData.ID "
                              "WHERE ItemDisplayInfoID = %1").arg(m_displayId);

      sqlResult iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), TEXTURE, 0);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      for (uint i = 0; i < iteminfos.values.size(); i++)
      {
        GameFile * texture = GAMEDIRECTORY.getFile(iteminfos.values[i][0].toInt());
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[getRegionForTexture(texture)] = texture;
        }
      }

      // now get geoset / model infos
      query = QString("SELECT GeoSetGroup1, ModelID, TextureID  FROM ItemDisplayInfo "
                      "LEFT JOIN ModelFileData ON Model1 = ModelFileData.ID "
                      "LEFT JOIN TextureFileData ON TextureItemID1 = TextureFileData.ID "
                      "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);

      iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), MERGED_MODEL, 1);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query geoset/model information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      m_itemGeosets[CG_GLOVES] = 1 + iteminfos.values[0][0].toInt();

      if (iteminfos.values[0][1].toInt() != 0)
        mergeModel(CS_GLOVES, iteminfos.values[0][1].toInt(), iteminfos.values[0][2].toInt());

      break;
    }
    case CS_HAND_RIGHT:
    case CS_HAND_LEFT:
    {
      QString query = QString("SELECT ModelID, TextureID FROM ItemDisplayInfo "
                              "LEFT JOIN ModelFileData ON Model1 = ModelFileData.ID "
                              "LEFT JOIN TextureFileData ON TextureItemID1 = TextureFileData.ID "
                              "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);

      sqlResult iteminfos = GAMEDATABASE.sqlQuery(query);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      updateItemModel(((m_slot == CS_HAND_RIGHT) ? ATT_RIGHT_PALM : ATT_LEFT_PALM), iteminfos.values[0][0].toInt(), iteminfos.values[0][1].toInt());

      break;
    }
    case CS_CAPE:
    {
      QString query = QString("SELECT TextureID, GeosetGroup1 FROM ItemDisplayInfo "
                              "LEFT JOIN TextureFileData ON TextureItemID1 = TextureFileData.ID "
                              "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);


      sqlResult iteminfos = GAMEDATABASE.sqlQuery(query);

      if (!iteminfos.valid || iteminfos.values.empty())
      {
        LOG_ERROR << "Impossible to query information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
        return;
      }

      GameFile * texture = GAMEDIRECTORY.getFile(iteminfos.values[0][0].toInt());
      if (texture)
      {
        TEXTUREMANAGER.add(texture);
        m_itemTextures[getRegionForTexture(texture)] = texture;
      }

      m_itemGeosets[CG_CAPE] = 1 + iteminfos.values[0][1].toInt();

      break;
    }
    case CS_TABARD:
    {
      if (isCustomizableTabard())
      {
        m_charModel->td.showCustom = true;
        m_itemGeosets[CG_TARBARD] = 2;

        GameFile * texture = GAMEDIRECTORY.getFile(m_charModel->td.GetBackgroundTex(CR_TORSO_UPPER));
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[CR_TABARD_1] = texture;
        }

        texture = GAMEDIRECTORY.getFile(m_charModel->td.GetBackgroundTex(CR_TORSO_LOWER));
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[CR_TABARD_2] = texture;
        }

        texture = GAMEDIRECTORY.getFile(m_charModel->td.GetIconTex(CR_TORSO_UPPER));
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[CR_TABARD_3] = texture;
        }

        texture = GAMEDIRECTORY.getFile(m_charModel->td.GetIconTex(CR_TORSO_LOWER));
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[CR_TABARD_4] = texture;
        }

        texture = GAMEDIRECTORY.getFile(m_charModel->td.GetBorderTex(CR_TORSO_UPPER));
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[CR_TABARD_5] = texture;
        }

        texture = GAMEDIRECTORY.getFile(m_charModel->td.GetBorderTex(CR_TORSO_LOWER));
        if (texture)
        {
          TEXTUREMANAGER.add(texture);
          m_itemTextures[CR_TABARD_6] = texture;
        }
      }
      else
      {
        m_charModel->td.showCustom = false;

        // query texture infos from ItemDisplayInfoMaterialRes
        QString query = QString("SELECT TextureID FROM ItemDisplayInfoMaterialRes "
                                "LEFT JOIN TextureFileData ON TextureFileDataID = TextureFileData.ID "
                                "WHERE ItemDisplayInfoID = %1").arg(m_displayId);

        sqlResult iteminfos = filterSQLResultForModel(GAMEDATABASE.sqlQuery(query), TEXTURE, 0);

        if (!iteminfos.valid || iteminfos.values.empty())
        {
          LOG_ERROR << "Impossible to query texture information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
          return;
        }

        for (uint i = 0; i < iteminfos.values.size(); i++)
        {
          GameFile * texture = GAMEDIRECTORY.getFile(iteminfos.values[i][0].toInt());
          if (texture)
          {
            TEXTUREMANAGER.add(texture);
            m_itemTextures[getRegionForTexture(texture)] = texture;
          }
        }

        // geosets
        query = QString("SELECT GeosetGroup1 FROM ItemDisplayInfo "
                        "WHERE ItemDisplayInfo.ID = %1").arg(m_displayId);

        iteminfos = GAMEDATABASE.sqlQuery(query);

        if (!iteminfos.valid || iteminfos.values.empty())
        {
          LOG_ERROR << "Impossible to query geoset/model information for item" << name() << "(id " << m_id << "- display id" << m_displayId << ")";
          return;
        }

        m_itemGeosets[CG_TARBARD] = 1 + iteminfos.values[0][0].toInt();
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

  switch (m_slot)
  {
    case CS_HEAD:
    {
      if (m_charModel->attachment)
      {
        m_charModel->attachment->delSlot(CS_HEAD);
        std::map<POSITION_SLOTS, WoWModel *>::iterator it = m_itemModels.find(ATT_HELMET);
        if (it != m_itemModels.end())
          m_charModel->attachment->addChild(it->second, ATT_HELMET, m_slot);
      }
      break;
    }
    case CS_SHOULDER:
    {
      if (m_charModel->attachment)
      {
        m_charModel->attachment->delSlot(CS_SHOULDER);

        std::map<POSITION_SLOTS, WoWModel *>::iterator it = m_itemModels.find(ATT_LEFT_SHOULDER);
        if (it != m_itemModels.end())
          m_charModel->attachment->addChild(it->second, ATT_LEFT_SHOULDER, m_slot);


        it = m_itemModels.find(ATT_RIGHT_SHOULDER);

        if (it != m_itemModels.end())
          m_charModel->attachment->addChild(it->second, ATT_RIGHT_SHOULDER, m_slot);
      }
      break;
    }
    case CS_HAND_RIGHT:
    {
      if (m_charModel->attachment)
      {
        m_charModel->attachment->delSlot(CS_HAND_RIGHT);

        std::map<POSITION_SLOTS, WoWModel *>::iterator it = m_itemModels.find(ATT_RIGHT_PALM);
        if (it != m_itemModels.end())
        {
          int attachement = ATT_RIGHT_PALM;
          const ItemRecord &item = items.getById(m_id);
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

        std::map<POSITION_SLOTS, WoWModel *>::iterator it = m_itemModels.find(ATT_LEFT_PALM);
        if (it != m_itemModels.end())
        {
          const ItemRecord &item = items.getById(m_id);
          int attachement = ATT_LEFT_PALM;

          if (item.type == IT_SHIELD)
            attachement = ATT_LEFT_WRIST;

          if (m_charModel->bSheathe &&  item.sheath != SHEATHETYPE_NONE)
            attachement = (POSITION_SLOTS)item.sheath;

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
      if (m_charModel->attachment)
      {
        m_charModel->attachment->delSlot(CS_BELT);

        if (m_mergedModel != 0)
        {
          m_charModel->mergeModel(m_mergedModel);
          m_mergedModel->setGeosetGroupDisplay(CG_BELT, 1);
        }

        std::map<POSITION_SLOTS, WoWModel *>::iterator it = m_itemModels.find(ATT_BELT_BUCKLE);
        if (it != m_itemModels.end())
          m_charModel->attachment->addChild(it->second, ATT_BELT_BUCKLE, m_slot);

        std::map<CharRegions, GameFile *>::iterator it2 = m_itemTextures.find(CR_LEG_UPPER);
        if (it2 != m_itemTextures.end())
          m_charModel->tex.addLayer(it2->second, CR_LEG_UPPER, SLOT_LAYERS[m_slot]);

        it2 = m_itemTextures.find(CR_TORSO_LOWER);
        if (it2 != m_itemTextures.end())
          m_charModel->tex.addLayer(it2->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);
      }
      break;
    }
    case CS_BOOTS:
    {
      if (m_charModel->attachment)
      {
        if (m_mergedModel != 0)
          m_charModel->mergeModel(m_mergedModel);

        auto geoIt = m_itemGeosets.find(CG_BOOTS);

        if (geoIt != m_itemGeosets.end())
        {
          // don't render boots behind robe
          {
            WoWItem * chestItem = m_charModel->getItem(CS_CHEST);
            if (chestItem->m_type != IT_ROBE) // maybe not handle when geoIt->second = 5 ?
            {
              m_charModel->cd.geosets[CG_BOOTS] = geoIt->second;
              if (m_mergedModel)
                m_mergedModel->setGeosetGroupDisplay(CG_BOOTS, 1);
            }
          }

          // handle 2000* group for hd models
          {
            RaceInfos infos;
            if (RaceInfos::getCurrent(m_charModel, infos) && infos.isHD)
            {
              m_charModel->cd.geosets[CG_HDFEET] = 2;
              if (m_mergedModel)
                m_mergedModel->setGeosetGroupDisplay(CG_HDFEET, 1);
            }
          }
        }

        std::map<CharRegions, GameFile *>::iterator texIt = m_itemTextures.find(CR_LEG_LOWER);
        if (texIt != m_itemTextures.end())
          m_charModel->tex.addLayer(texIt->second, CR_LEG_LOWER, SLOT_LAYERS[m_slot]);

        if (!m_charModel->cd.showFeet)
        {
          texIt = m_itemTextures.find(CR_FOOT);
          if (texIt != m_itemTextures.end())
            m_charModel->tex.addLayer(texIt->second, CR_FOOT, SLOT_LAYERS[m_slot]);
        }
      }
      break;
    }
    case CS_PANTS:
    {
      if (m_mergedModel != 0)
        m_charModel->mergeModel(m_mergedModel);

      std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_KNEEPADS);
      if (geoIt != m_itemGeosets.end())
      {
        m_charModel->cd.geosets[CG_KNEEPADS] = geoIt->second;
        if (m_mergedModel)
          m_mergedModel->setGeosetGroupDisplay(CG_KNEEPADS, 1);
      }

      geoIt = m_itemGeosets.find(CG_TROUSERS);

      if (geoIt != m_itemGeosets.end())
      {
        // apply trousers geosets only if character is not already wearing a robe
        const ItemRecord &item = items.getById(m_charModel->getItem(CS_CHEST)->id());

        if (item.type != IT_ROBE)
        {
          m_charModel->cd.geosets[CG_TROUSERS] = geoIt->second;
          if (m_mergedModel)
            m_mergedModel->setGeosetGroupDisplay(CG_TROUSERS, 1);
        }
      }

      auto texIt =  m_itemTextures.find(CR_LEG_UPPER);
      if (texIt != m_itemTextures.end())
        m_charModel->tex.addLayer(texIt->second, CR_LEG_UPPER, SLOT_LAYERS[m_slot]);

      texIt = m_itemTextures.find(CR_LEG_LOWER);
      if (texIt != m_itemTextures.end())
        m_charModel->tex.addLayer(texIt->second, CR_LEG_LOWER, SLOT_LAYERS[m_slot]);

      break;
    }
    case CS_SHIRT:
    case CS_CHEST:
    {
      if (m_mergedModel != 0)
      {
        m_charModel->mergeModel(m_mergedModel);
        m_mergedModel->setGeosetGroupDisplay(CG_WRISTBANDS, 1);
        m_mergedModel->setGeosetGroupDisplay(CG_TROUSERS, 1);
        m_mergedModel->setGeosetGroupDisplay(CG_PANTS, 1);
      }

      std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_WRISTBANDS);
      if (geoIt != m_itemGeosets.end())
        m_charModel->cd.geosets[CG_WRISTBANDS] = geoIt->second;

      std::map<CharRegions, GameFile *>::iterator it = m_itemTextures.find(CR_ARM_UPPER);
      if (it != m_itemTextures.end())
        m_charModel->tex.addLayer(it->second, CR_ARM_UPPER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_ARM_LOWER);
      if (it != m_itemTextures.end())
        m_charModel->tex.addLayer(it->second, CR_ARM_LOWER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_TORSO_UPPER);
      if (it != m_itemTextures.end())
        m_charModel->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_TORSO_LOWER);
      if (it != m_itemTextures.end())
        m_charModel->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);


      geoIt = m_itemGeosets.find(CG_TROUSERS);
      if (geoIt != m_itemGeosets.end())
      {
        m_charModel->cd.geosets[CG_TROUSERS] = geoIt->second;

        it = m_itemTextures.find(CR_LEG_UPPER);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_LEG_UPPER, SLOT_LAYERS[m_slot]);

        it = m_itemTextures.find(CR_LEG_LOWER);
        if (it != m_itemTextures.end())
          m_charModel->tex.addLayer(it->second, CR_LEG_LOWER, SLOT_LAYERS[m_slot]);
      }
      break;
    }
    case CS_BRACERS:
    {
      std::map<CharRegions, GameFile *>::iterator it = m_itemTextures.find(CR_ARM_LOWER);
      if (it != m_itemTextures.end())
        m_charModel->tex.addLayer(it->second, CR_ARM_LOWER, SLOT_LAYERS[m_slot]);
      break;
    }
    case CS_GLOVES:
    {
      if (m_mergedModel != 0)
        m_charModel->mergeModel(m_mergedModel);

      std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_GLOVES);
      if (geoIt != m_itemGeosets.end())
      {
        m_charModel->cd.geosets[CG_GLOVES] = geoIt->second;
        if (m_mergedModel)
          m_mergedModel->setGeosetGroupDisplay(CG_GLOVES, 1);
      }

      std::map<CharRegions, GameFile *>::iterator texIt = m_itemTextures.find(CR_ARM_LOWER);

      int layer = SLOT_LAYERS[m_slot];

      // if we are wearing a robe, render gloves first in texture compositing
      // only if GeoSetGroup1 is 0 (from item displayInfo db) which corresponds to stored geoset equals to 1
      WoWItem * chestItem = m_charModel->getItem(CS_CHEST);
      if ((chestItem->m_type == IT_ROBE) && (geoIt->second == 1))
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
      std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_CAPE);
      if (geoIt != m_itemGeosets.end())
        m_charModel->cd.geosets[CG_CAPE] = geoIt->second;

      std::map<CharRegions, GameFile *>::iterator it = m_itemTextures.find(CR_CAPE);
      if (it != m_itemTextures.end())
        m_charModel->updateTextureList(it->second, TEXTURE_CAPE);
      break;
    }
    case CS_TABARD:
    {
      m_charModel->cd.geosets[CG_TARBARD] = m_itemGeosets[CG_TARBARD];

      if (isCustomizableTabard())
      {
        std::map<CharRegions, GameFile *>::iterator it = m_itemTextures.find(CR_TABARD_1);
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
        std::map<CharRegions, GameFile *>::iterator it = m_itemTextures.find(CR_TORSO_UPPER);
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

void WoWItem::save(QXmlStreamWriter & stream)
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

  int nbValuesRead = 0;
  while (!reader.atEnd() && nbValuesRead != 3)
  {
    if (reader.isStartElement())
    {
      if (reader.name() == "slot")
      {
        unsigned int slot = reader.attributes().value("value").toString().toUInt();

        if (slot == m_slot)
        {
          while (!reader.atEnd() && nbValuesRead != 3)
          {
            if (reader.isStartElement())
            {
              if (reader.name() == "id")
              {
                int id = reader.attributes().value("value").toString().toInt();
                nbValuesRead++;
                if (id != -1)
                  setId(id);
              }

              if (reader.name() == "displayId")
              {
                int id = reader.attributes().value("value").toString().toInt();
                nbValuesRead++;
                if (m_id == -1)
                  setDisplayId(id);
              }

              if (reader.name() == "level")
              {
                int level = reader.attributes().value("value").toString().toInt();
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
      m_charModel->td.load(reader);
      load(); // refresh tabard textures
    }
  }
}

void WoWItem::updateItemModel(POSITION_SLOTS pos, int modelId, int textureId)
{
  WoWModel *m = new WoWModel(GAMEDIRECTORY.getFile(modelId), true);

  if (m->ok)
  {
    m_itemModels[pos] = m;
    GameFile * texture = GAMEDIRECTORY.getFile(textureId);
    if (texture)
      m->updateTextureList(texture, TEXTURE_ITEM);
    else
      LOG_ERROR << "Error during item update" << m_id << "(display id" << m_displayId << "). Texture" << textureId << "can't be loaded";
  }
}

void WoWItem::mergeModel(CharSlots slot, int modelId, int textureId)
{
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
}

CharRegions WoWItem::getRegionForTexture(GameFile * file) const
{
  CharRegions result = CR_UNK8;

  if (file)
  {
    QString fullname = file->fullname();

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

sqlResult WoWItem::filterSQLResultForModel(sqlResult & sql, FilteringType filterType, uint itemToFilter) const
{
  if (!sql.valid || (sql.values.size() <= 1) || itemToFilter >= sql.values[0].size())
    return sql;

  sqlResult result;
  result.valid = sql.valid;

  RaceInfos infos;
  RaceInfos::getCurrent(m_charModel, infos);

  QString filter = "_";
  switch (filterType)
  {
  case MODEL:
    filter += QString::fromStdString(infos.prefix);
    filter += (infos.sexid == 0) ? "m" : "f";
    break;
  case MERGED_MODEL:
    filter += QString::fromStdString(infos.prefix);
    filter += "_";
    filter += (infos.sexid == 0) ? "m" : "f";
    break;
  case TEXTURE:
    filter += "[";
    filter += (infos.sexid == 0) ? "m" : "f";
    filter += "u";
    filter += "]";
    break;
  }

  filter += "\\.";

#if DEBUG_FILTERING > 0
  LOG_INFO << __FUNCTION__ << filter;
#endif

  QRegularExpression re(filter);

  for (uint i = 0; i < sql.values.size(); i++)
  {
    GameFile * f = GAMEDIRECTORY.getFile(sql.values[i][itemToFilter].toInt());
    if (f)
    {
      QRegularExpressionMatch r = re.match(f->fullname());

      if (r.hasMatch())
        result.values.push_back(sql.values[i]);
    }
  }

#if DEBUG_FILTERING > 0
  LOG_INFO << __FUNCTION__ << "---- BEFORE -----";
  for (uint i = 0; i < sql.values.size(); i++)
  {
    GameFile * f = GAMEDIRECTORY.getFile(sql.values[i][itemToFilter].toInt());
    if (f)
      LOG_INFO << f->fullname();
  }

  LOG_INFO << __FUNCTION__ << "---- AFTER -----";
  for (uint i = 0; i < result.values.size(); i++)
  {
    GameFile * f = GAMEDIRECTORY.getFile(result.values[i][itemToFilter].toInt());
    if (f)
      LOG_INFO << f->fullname();
  }
#endif

  return result;
}
