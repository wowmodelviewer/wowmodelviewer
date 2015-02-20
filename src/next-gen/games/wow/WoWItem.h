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
 * WoWItem.h
 *
 *  Created on: 5 feb. 2015
 *      Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _WOWITEM_H_
#define _WOWITEM_H_

#include <map>
#include <string>

#include "enums.h"

#include "metaclasses/Component.h"

class WoWModel;

class WoWItem : public Component
{
  public:
    WoWItem(CharSlots slot);

    void setId(int id);
    int id() { return m_id; }

    void setDisplayId(int id);

    CharSlots slot() { return m_slot; }

    int quality() { return m_quality; }

    void refresh();

    void onParentSet(Component *);

    void load();

  private:
    void unload();

    bool isCustomizableTabard();

    WoWModel * m_model;

    int m_id;
    int m_displayId;
    int m_quality;

    CharSlots m_slot;

    static std::map<CharSlots,int> SLOT_LAYERS;
    static std::map<CharSlots,int> initSlotLayers();

    std::map<POSITION_SLOTS, WoWModel *> m_itemModels;
    std::map<CharRegions, std::string> m_itemTextures;
    std::map<CharGeosets, int> m_itemGeosets;

};


#endif /* _WOWITEM_H_ */
