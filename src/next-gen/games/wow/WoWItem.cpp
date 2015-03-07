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

#include "CASCFolder.h"
#include "GameDatabase.h"
#include "RaceInfos.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#include "database.h" // items
#include "globalvars.h"
#include "modelviewer.h"

#include <QString>
#include "Attachment.h"

map<int,int> create_map()
{
  map<int,int> m;
  m[1] = 2;
  m[3] = 4;
  m[5] = 6;
  return m;
}

map<CharSlots,int> WoWItem::SLOT_LAYERS = WoWItem::initSlotLayers();





WoWItem::WoWItem(CharSlots slot)
: m_model(0), m_id(-1), m_displayId(-1), m_quality(0), m_slot(slot)
{
  setName("---- None ----");
}

void WoWItem::setId(int id)
{
  if(id != m_id)
  {
    m_id = id;

    if(m_id == 0)
    {
      unload();
      // reset name and quality
      setName("---- None ----");
      m_quality = 0;

      if(m_slot == CS_HAND_RIGHT)
        m_model->charModelDetails.closeRHand = false;

      if(m_slot == CS_HAND_LEFT)
        m_model->charModelDetails.closeLHand = false;

      return;
    }

    QString query = QString("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = (SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %1)")
           .arg(id);

    sqlResult iteminfos = GAMEDATABASE.sqlQuery(query.toStdString());

    if(iteminfos.valid && !iteminfos.values.empty())
      m_displayId = atoi(iteminfos.values[0][0].c_str());

    ItemRecord itemRcd = items.getById(id);
    setName(itemRcd.name.c_str());
    m_quality = itemRcd.quality;
    load();
  }
}

void WoWItem::setDisplayId(int id)
{
  if(m_displayId != id)
  {
    m_id = -1;
    m_displayId = id; // to update from database;
    setName("NPC Item");
    load();
  }
}

void WoWItem::onParentSet(Component * parent)
{
  m_model = dynamic_cast<WoWModel *>(parent);
}

std::map<CharSlots,int> WoWItem::initSlotLayers()
{
  std::map<CharSlots,int> result;
  result[CS_SHIRT] = 10;
  result[CS_HEAD] = 11;
  result[CS_NECK] = 12;
  result[CS_SHOULDER] = 13;
  result[CS_PANTS] = 14;
  result[CS_BOOTS] = 15;
  result[CS_CHEST] = 16;
  result[CS_TABARD] = 17;
  result[CS_BELT] = 18;
  result[CS_BRACERS] = 19;
  result[CS_GLOVES] = 20;
  result[CS_HAND_RIGHT] = 21;
  result[CS_HAND_LEFT] = 22;
  result[CS_CAPE] = 23;
  result[CS_QUIVER] = 24;

  return result;
}

void WoWItem::unload()
{
  // delete models and clear map
  for(std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.begin(),
      itEnd = itemModels.end();
      it != itEnd ;
      ++it)
  {
    delete it->second;
  }
  itemModels.clear();

  // release textures and clear map
  for(std::map<CharRegions, std::string>::iterator it = m_itemTextures.begin(),
      itEnd = m_itemTextures.end();
      it != itEnd ;
      ++it)
  {
    texturemanager.delbyname(it->second);
  }
  m_itemTextures.clear();

  // clear map
  m_itemGeosets.clear();

  // remove any existing attachement
  g_modelViewer->charControl->charAtt->delSlot(m_slot);
}

void WoWItem::load()
{
  unload();

  if(!m_model) // no parent => give up
    return;

  if(m_id == 0) // no equipment, just return
    return;

  RaceInfos infos;
  if(!RaceInfos::getCurrent(std::string(m_model->wxname.mb_str()), infos))
    return;

  QString query = QString("SELECT Model1,Model2, \
       FD10.path AS Model1TexPath, FD10.name AS Model1TexName, \
       FD11.path AS Model2TexPath, FD11.name AS Model2TexName, \
       GeosetGroup1, GeosetGroup2, GeosetGroup3, \
       HelmetGeoSetVis1,HelmetGeoSetVis2, \
       FD1.path AS UpperArmTexPath, FD1.name AS UpperArmTexName, \
       FD2.path AS LowerArmTexPath, FD2.name AS LowerArmTexName, \
       FD3.path AS HandsTexPath, FD3.name AS HandsTexName, \
       FD4.path AS UpperTorsoTexPath, FD4.name AS UpperTorsoTexName, \
       FD5.path AS LowerTorsoTexPath, FD5.name AS LowerTorsoTexName, \
       FD6.path AS UpperLegTexPath, FD6.name AS UpperLegTexName, \
       FD7.path AS LowerLegTexPath, FD7.name AS LowerLegTexName, \
       FD8.path AS FootTexPath, FD8.name AS FootTexName, \
       FD9.path AS AccessoryTexPath, FD9.name AS AccessoryTexName \
       FROM ItemDisplayInfo \
       LEFT JOIN TextureFileData TFD1 ON TFD1.TextureItemID = TextureID1 AND TFD1.TextureType != %1 LEFT JOIN FileData FD1 ON TFD1.FileDataID = FD1.ID \
       LEFT JOIN TextureFileData TFD2 ON TFD2.TextureItemID = TextureID2 AND TFD2.TextureType != %1 LEFT JOIN FileData FD2 ON TFD2.FileDataID = FD2.ID \
       LEFT JOIN TextureFileData TFD3 ON TFD3.TextureItemID = TextureID3 AND TFD3.TextureType != %1 LEFT JOIN FileData FD3 ON TFD3.FileDataID = FD3.ID \
       LEFT JOIN TextureFileData TFD4 ON TFD4.TextureItemID = TextureID4 AND TFD4.TextureType != %1 LEFT JOIN FileData FD4 ON TFD4.FileDataID = FD4.ID \
       LEFT JOIN TextureFileData TFD5 ON TFD5.TextureItemID = TextureID5 AND TFD5.TextureType != %1 LEFT JOIN FileData FD5 ON TFD5.FileDataID = FD5.ID \
       LEFT JOIN TextureFileData TFD6 ON TFD6.TextureItemID = TextureID6 AND TFD6.TextureType != %1 LEFT JOIN FileData FD6 ON TFD6.FileDataID = FD6.ID \
       LEFT JOIN TextureFileData TFD7 ON TFD7.TextureItemID = TextureID7 AND TFD7.TextureType != %1 LEFT JOIN FileData FD7 ON TFD7.FileDataID = FD7.ID \
       LEFT JOIN TextureFileData TFD8 ON TFD8.TextureItemID = TextureID8 AND TFD8.TextureType != %1 LEFT JOIN FileData FD8 ON TFD8.FileDataID = FD8.ID \
       LEFT JOIN TextureFileData TFD9 ON TFD9.TextureItemID = TextureID9 AND TFD9.TextureType != %1 LEFT JOIN FileData FD9 ON TFD9.FileDataID = FD9.ID \
       LEFT JOIN TextureFileData TFD10 ON TFD10.TextureItemID = TextureItemID1 AND TFD10.TextureType != %1 LEFT JOIN FileData FD10 ON TFD10.FileDataID = FD10.ID \
       LEFT JOIN TextureFileData TFD11 ON TFD11.TextureItemID = TextureItemID2 AND TFD11.TextureType != %1 LEFT JOIN FileData FD11 ON TFD11.FileDataID = FD11.ID \
       WHERE ItemDisplayInfo.ID = %2")
     .arg((infos.sexid == 0)?1:0)
     .arg(m_displayId);

  sqlResult iteminfos = GAMEDATABASE.sqlQuery(query.toStdString());

  if(!iteminfos.valid || iteminfos.values.empty())
    return;

  switch(m_slot)
  {
  case CS_HEAD:
  {
    WoWModel *m = NULL;
    GLuint tex;
    std::string model = iteminfos.values[0][0];
    // remove .mdx
    model = model.substr(0, model.length()-4);
    // add race prefix
    model += "_";
    model += infos.prefix;
    // add sex suffix
    model += ((infos.sexid == 0)?"M":"F");
    // add .m2
    model += ".m2";
    model = CASCFOLDER.getFullPathForFile(model);

    m = new WoWModel(model, true);

    if (m->ok)
    {
      itemModels[ATT_HELMET] = m;
      std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
      tex = texturemanager.add(texture);
      for (size_t x=0;x<m->TextureList.size();x++)
      {
        if (m->TextureList[x] == wxString(wxT("Special_2")))
        {
          wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
          m->TextureList[x] = texture;
        }
      }
      m->replaceTextures[TEXTURE_CAPE] = tex;
    }

    break;
  }
  case CS_NECK:
    break;
  case CS_SHOULDER:
  {

    WoWModel *m = NULL;
    GLuint tex;

    // left shoulder
    std::string model = iteminfos.values[0][0];
    model = model.substr(0, model.length()-4); // remove .mdx
    model += ".m2"; // add .m2
    model = CASCFOLDER.getFullPathForFile(model);

    m = new WoWModel(model, true);

    if (m->ok)
    {
      itemModels[ATT_LEFT_SHOULDER] = m;
      std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
      tex = texturemanager.add(texture);
      for (size_t x=0;x<m->TextureList.size();x++)
      {
        if (m->TextureList[x] == wxString(wxT("Special_2")))
        {
          wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
          m->TextureList[x] = texture;
        }
      }
      m->replaceTextures[TEXTURE_CAPE] = tex;
    }

    // right shoulder
    model = iteminfos.values[0][1];
    model = model.substr(0, model.length()-4); // remove .mdx
    model += ".m2"; // add .m2
    model = CASCFOLDER.getFullPathForFile(model);

    m = new WoWModel(model, true);

    if (m->ok)
    {
      itemModels[ATT_RIGHT_SHOULDER] = m;
      std::string texture = iteminfos.values[0][4] + iteminfos.values[0][5];
      tex = texturemanager.add(texture);
      for (size_t x=0;x<m->TextureList.size();x++)
      {
        if (m->TextureList[x] == wxString(wxT("Special_2")))
        {
          wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
          m->TextureList[x] = texture;
        }
      }
      m->replaceTextures[TEXTURE_CAPE] = tex;
    }
    break;
  }
  case CS_BOOTS:
  {
    m_itemGeosets[CG_BOOTS] = 1 + atoi(iteminfos.values[0][6].c_str());

    std::string texture = iteminfos.values[0][23] + iteminfos.values[0][24];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_LEG_LOWER] = texture;
    }

    texture = iteminfos.values[0][25] + iteminfos.values[0][26];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_FOOT] = texture;
    }
  }
  break;
  case CS_BELT:
  {
    std::string texture = iteminfos.values[0][21] + iteminfos.values[0][22];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_LEG_UPPER] = texture;
    }
  }
  break;
  case CS_PANTS:
  {
    m_itemGeosets[CG_KNEEPADS] = 1 + atoi(iteminfos.values[0][7].c_str());

    std::string texture = iteminfos.values[0][21] + iteminfos.values[0][22];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_LEG_UPPER] = texture;
    }

    texture = iteminfos.values[0][23] + iteminfos.values[0][24];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_LEG_LOWER] = texture;
    }

    if (atoi(iteminfos.values[0][8].c_str())==1)
      m_itemGeosets[CG_TROUSERS] = 1 + atoi(iteminfos.values[0][8].c_str());

    break;
  }
  case CS_SHIRT:
  case CS_CHEST:
  {
    m_itemGeosets[CG_WRISTBANDS] = 1 + atoi(iteminfos.values[0][6].c_str());

    std::string texture = iteminfos.values[0][11] + iteminfos.values[0][12];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_ARM_UPPER] = texture;
    }

    texture = iteminfos.values[0][13] + iteminfos.values[0][14];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_ARM_LOWER] = texture;
    }

    texture = iteminfos.values[0][17] + iteminfos.values[0][18];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_TORSO_UPPER] = texture;
    }

    texture = iteminfos.values[0][19] + iteminfos.values[0][20];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_TORSO_LOWER] = texture;
    }

    const ItemRecord &item = items.getById(m_id);

    if (item.type == IT_ROBE || atoi(iteminfos.values[0][8].c_str())==1)
    {
      m_itemGeosets[CG_TROUSERS] = 1 + atoi(iteminfos.values[0][8].c_str());

      texture = iteminfos.values[0][21] + iteminfos.values[0][22];
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_LEG_UPPER] = texture;
      }

      texture = iteminfos.values[0][23] + iteminfos.values[0][24];
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_LEG_LOWER] = texture;
      }
    }
  }
  break;
  case CS_BRACERS:
  {
    std::string texture = iteminfos.values[0][13] + iteminfos.values[0][14];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_ARM_LOWER] = texture;
    }
  }
  break;
  case CS_GLOVES:
  {
    m_itemGeosets[CG_GLOVES] = 1 + atoi(iteminfos.values[0][6].c_str());

    std::string texture = iteminfos.values[0][13] + iteminfos.values[0][14];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_ARM_LOWER] = texture;
    }

    texture = iteminfos.values[0][15] + iteminfos.values[0][16];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_HAND] = texture;
    }
  }
  break;
  case CS_HAND_RIGHT:
  {
    WoWModel *m = NULL;
    GLuint tex;

    std::string itemModel = iteminfos.values[0][0];
    itemModel = itemModel.substr(0, itemModel.length()-4); // remove .mdx
    itemModel += ".m2"; // add .m2
    itemModel = CASCFOLDER.getFullPathForFile(itemModel);

    m = new WoWModel(itemModel, true);

    if (m->ok)
    {
      itemModels[ATT_RIGHT_PALM] = m;

      std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
      tex = texturemanager.add(texture);
      for (size_t x=0;x<m->TextureList.size();x++)
      {
        if (m->TextureList[x] == wxString(wxT("Special_2")))
        {
          wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
          m->TextureList[x] = texture;
        }
      }
      m->replaceTextures[TEXTURE_CAPE] = tex;
    }
  }
  break;
  case CS_HAND_LEFT:
  {
    WoWModel *m = NULL;
    GLuint tex;

    std::string itemModel = iteminfos.values[0][0];
    itemModel = itemModel.substr(0, itemModel.length()-4); // remove .mdx
    itemModel += ".m2"; // add .m2
    itemModel = CASCFOLDER.getFullPathForFile(itemModel);

    m = new WoWModel(itemModel, true);

    if (m->ok)
    {
      itemModels[ATT_LEFT_PALM] = m;

      std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
      tex = texturemanager.add(texture);
      for (size_t x=0;x<m->TextureList.size();x++)
      {
        if (m->TextureList[x] == wxString(wxT("Special_2")))
        {
          wxLogMessage(wxT("Replacing ID1's %s with %s"),m->TextureList[x].c_str(),texture.c_str());
          m->TextureList[x] = texture;
        }
      }
      m->replaceTextures[TEXTURE_CAPE] = tex;
    }
    break;
  }
  case CS_CAPE:
  {
    m_itemGeosets[CG_CAPE] = 1 + atoi(iteminfos.values[0][6].c_str());

    std::string texture = iteminfos.values[0][2] + iteminfos.values[0][3];
    if(!texture.empty())
    {
      texturemanager.add(texture);
      m_itemTextures[CR_CAPE] = texture;
    }
  }
  break;
  case CS_TABARD:
  {
    m_itemGeosets[CG_TARBARD] = 2;
    if(isCustomizableTabard())
    {
      g_modelViewer->charControl->td.showCustom = true;
      std::string texture = g_modelViewer->charControl->td.GetBackgroundTex(CR_TORSO_UPPER).mb_str();
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_TABARD_1] = texture;
      }

      texture = g_modelViewer->charControl->td.GetBackgroundTex(CR_TORSO_LOWER).mb_str();
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_TABARD_2] = texture;
      }

      texture = g_modelViewer->charControl->td.GetIconTex(CR_TORSO_UPPER).mb_str();
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_TABARD_3] = texture;
      }

      texture = g_modelViewer->charControl->td.GetIconTex(CR_TORSO_LOWER).mb_str();
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_TABARD_4] = texture;
      }

      texture = g_modelViewer->charControl->td.GetBorderTex(CR_TORSO_UPPER).mb_str();
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_TABARD_5] = texture;
      }

      texture = g_modelViewer->charControl->td.GetBorderTex(CR_TORSO_LOWER).mb_str();
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_TABARD_6] = texture;
      }
    }
    else
    {
      g_modelViewer->charControl->td.showCustom = false;
      std::string texture = iteminfos.values[0][17] + iteminfos.values[0][18];
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_TORSO_UPPER] = texture;
      }

      texture = iteminfos.values[0][19] + iteminfos.values[0][20];
      if(!texture.empty())
      {
        texturemanager.add(texture);
        m_itemTextures[CR_TORSO_LOWER] = texture;
      }
    }
  }
  break;
  case CS_QUIVER:
    break;
  default:
    break;
  }
}

void WoWItem::refresh()
{
  if(m_id == 0) // no item equipped, give up
    return;

  switch(m_slot)
  {
  case CS_HEAD:
  {
    g_modelViewer->charControl->charAtt->delSlot(CS_HEAD);
    std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.find(ATT_HELMET);
    if(it != itemModels.end())
      g_modelViewer->charControl->charAtt->addChild(it->second, ATT_HELMET, m_slot);
    break;
  }
  case CS_SHOULDER:
  {
    g_modelViewer->charControl->charAtt->delSlot(CS_SHOULDER);

    std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.find(ATT_LEFT_SHOULDER);
    if(it != itemModels.end())
      g_modelViewer->charControl->charAtt->addChild(it->second, ATT_LEFT_SHOULDER, m_slot);

    it = itemModels.find(ATT_RIGHT_SHOULDER);

    if(it != itemModels.end())
      g_modelViewer->charControl->charAtt->addChild(it->second, ATT_RIGHT_SHOULDER, m_slot);

    break;
  }
  case CS_HAND_RIGHT:
  {
    g_modelViewer->charControl->charAtt->delSlot(CS_HAND_RIGHT);

    std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.find(ATT_RIGHT_PALM);
    if(it != itemModels.end())
    {
      int attachement = ATT_RIGHT_PALM;
      const ItemRecord &item = items.getById(m_id);
      if(g_modelViewer->charControl->bSheathe &&  item.sheath != SHEATHETYPE_NONE)
      {
        // make the weapon cross
        if (item.sheath == ATT_LEFT_BACK_SHEATH)
          attachement = ATT_RIGHT_BACK_SHEATH;
        if (item.sheath == ATT_LEFT_BACK)
          attachement = ATT_RIGHT_BACK;
        if (item.sheath == ATT_LEFT_HIP_SHEATH)
          attachement = ATT_RIGHT_HIP_SHEATH;
      }

      if(g_modelViewer->charControl->bSheathe)
        m_model->charModelDetails.closeRHand = false;
      else
        m_model->charModelDetails.closeRHand = true;

      g_modelViewer->charControl->charAtt->addChild(it->second, attachement, m_slot);
    }
    break;
  }
  case CS_HAND_LEFT:
  {
    g_modelViewer->charControl->charAtt->delSlot(CS_HAND_LEFT);

    std::map<POSITION_SLOTS, WoWModel *>::iterator it = itemModels.find(ATT_LEFT_PALM);
    if(it != itemModels.end())
    {
      const ItemRecord &item = items.getById(m_id);
      int attachement = ATT_LEFT_PALM;

      if(item.type == IT_SHIELD)
        attachement = ATT_LEFT_WRIST;

      if(g_modelViewer->charControl->bSheathe &&  item.sheath != SHEATHETYPE_NONE)
        attachement = (POSITION_SLOTS)item.sheath;

      if(g_modelViewer->charControl->bSheathe || item.type == IT_SHIELD)
        m_model->charModelDetails.closeLHand = false;
      else
        m_model->charModelDetails.closeLHand = true;

      g_modelViewer->charControl->charAtt->addChild(it->second, attachement, m_slot);
    }
    break;
  }
  case CS_BELT:
  {
    std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_LEG_UPPER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_LEG_UPPER, SLOT_LAYERS[m_slot]);
    break;
  }
  case CS_BOOTS:
  {
    std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_BOOTS);
    if(geoIt != m_itemGeosets.end())
      g_modelViewer->charControl->model->cd.geosets[CG_BOOTS] = geoIt->second;

    std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_LEG_LOWER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_LEG_LOWER, SLOT_LAYERS[m_slot]);

    if (!m_model->cd.showFeet)
    {
      it = m_itemTextures.find(CR_FOOT);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_FOOT, SLOT_LAYERS[m_slot]);
    }

    break;
  }
  case CS_PANTS:
  {
    std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_KNEEPADS);
    if(geoIt != m_itemGeosets.end())
      g_modelViewer->charControl->model->cd.geosets[CG_KNEEPADS] = geoIt->second;

    geoIt = m_itemGeosets.find(CG_TROUSERS);
    if(geoIt != m_itemGeosets.end())
      g_modelViewer->charControl->model->cd.geosets[CG_TROUSERS] = geoIt->second;

    std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_LEG_UPPER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_LEG_UPPER, SLOT_LAYERS[m_slot]);

    it = m_itemTextures.find(CR_LEG_LOWER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_LEG_LOWER, SLOT_LAYERS[m_slot]);

    break;
  }
  case CS_SHIRT:
  case CS_CHEST:
  {
    std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_WRISTBANDS);
    if(geoIt != m_itemGeosets.end())
      g_modelViewer->charControl->model->cd.geosets[CG_WRISTBANDS] = geoIt->second;

    std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_ARM_UPPER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_ARM_UPPER, SLOT_LAYERS[m_slot]);

    it = m_itemTextures.find(CR_ARM_LOWER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_ARM_LOWER, SLOT_LAYERS[m_slot]);

    it = m_itemTextures.find(CR_TORSO_UPPER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

    it = m_itemTextures.find(CR_TORSO_LOWER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);


    geoIt = m_itemGeosets.find(CG_TROUSERS);
    if(geoIt != m_itemGeosets.end())
    {
      g_modelViewer->charControl->model->cd.geosets[CG_TROUSERS] = geoIt->second;

      it = m_itemTextures.find(CR_LEG_UPPER);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_LEG_UPPER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_LEG_LOWER);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_LEG_LOWER, SLOT_LAYERS[m_slot]);
    }
    break;
  }
  case CS_BRACERS:
  {
    std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_ARM_LOWER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_ARM_LOWER, SLOT_LAYERS[m_slot]);
    break;
  }
  case CS_GLOVES:
  {
    std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_GLOVES);
    if(geoIt != m_itemGeosets.end())
      g_modelViewer->charControl->model->cd.geosets[CG_GLOVES] = geoIt->second;

    std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_ARM_LOWER);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_ARM_LOWER, SLOT_LAYERS[m_slot]);

    it = m_itemTextures.find(CR_HAND);
    if(it != m_itemTextures.end())
      m_model->tex.addLayer(it->second, CR_HAND, SLOT_LAYERS[m_slot]);
    break;
  }
  case CS_CAPE:
  {
    std::map<CharGeosets, int>::iterator geoIt = m_itemGeosets.find(CG_CAPE);
    if(geoIt != m_itemGeosets.end())
      g_modelViewer->charControl->model->cd.geosets[CG_CAPE] = geoIt->second;

    std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_CAPE);
    if(it != m_itemTextures.end())
    {
      g_modelViewer->charControl->capeTex = texturemanager.add(it->second);
      g_modelViewer->charControl->model->UpdateTextureList(it->second, TEXTURE_CAPE);
    }
    break;
  }
  case CS_TABARD:
  {
    g_modelViewer->charControl->model->cd.geosets[CG_TARBARD] = m_itemGeosets[CG_TARBARD];

    if(isCustomizableTabard())
    {
      std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_TABARD_1);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_TABARD_2);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_TABARD_3);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_TABARD_4);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_TABARD_5);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_TABARD_6);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);

    }
    else
    {
      std::map<CharRegions, std::string>::iterator it = m_itemTextures.find(CR_TORSO_UPPER);
      if(it != m_itemTextures.end())
        m_model->tex.addLayer(it->second, CR_TORSO_UPPER, SLOT_LAYERS[m_slot]);

      it = m_itemTextures.find(CR_TORSO_LOWER);
           if(it != m_itemTextures.end())
             m_model->tex.addLayer(it->second, CR_TORSO_LOWER, SLOT_LAYERS[m_slot]);
    }
    break;
  }
  default:
    break;
  }
}

bool WoWItem::isCustomizableTabard()
{
  return (m_id == 5976  || // Guild Tabard
          m_id == 69209 || // Illustrious Guild Tabard
          m_id == 69210);  // Renowned Guild Tabard
}
