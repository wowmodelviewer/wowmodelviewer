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

#pragma once

#include <map>
#include <string>
#include <vector>
#include "WoWDatabase.h"
#include "GameFile.h"
#include "wow_enums.h"
#include "metaclasses/Component.h"

class Attachment;
class WoWModel;
class QXmlStreamWriter;
class QXmlStreamReader;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WOWITEM_API_ __declspec(dllexport)
#    else
#        define _WOWITEM_API_ __declspec(dllimport)
#    endif
#else
#    define _WOWITEM_API_
#endif

class _WOWITEM_API_ WoWItem : public Component
{
public:
	WoWItem(CharSlots slot);

	void setId(int id);
	int id() const { return id_; }

	void setDisplayId(int id);
	void setLevel(int level);
	void setModifierId(int id);

	CharSlots slot() const { return slot_; }

	int quality() const { return quality_; }

	void refresh();

	void onParentSet(Component*) override;

	void load();

	unsigned int nbLevels() const { return nbLevels_; }

	std::map<POSITION_SLOTS, WoWModel*> models() const { return itemModels_; }

	void save(QXmlStreamWriter&) const;
	void load(QString&);

private:
	void unload();

	bool isCustomizableTabard() const;

	WoWModel* charModel_ = nullptr;

	int id_ = -1;
	int displayId_ = -1;
	int quality_ = 0;
	int level_ = 0;
	int type_ = 0;
	int displayFlags_ = 0;
	unsigned int nbLevels_ = 0;

	CharSlots slot_;

	static std::map<CharSlots, int> SLOT_LAYERS_;
	static std::map<std::string, int> MODELID_OFFSETS_;

	std::map<CharRegions, GameFile*> itemTextures_;
	std::map<CharGeosets, int> itemGeosets_;
	std::map<int, int> levelDisplayMap_;
	std::map<int, int> modifierIdDisplayMap_;
	std::map<POSITION_SLOTS, WoWModel*> itemModels_;
	WoWModel* mergedModel_ = nullptr;

	void updateItemModel(POSITION_SLOTS pos, int modelId, int textureId);
	void mergeModel(CharSlots slot, int modelId, int textureId);

	CharRegions getRegionForTexture(GameFile* file) const;

	bool queryItemInfo(const QString& query, sqlResult& result) const;

	int getCustomModelId(size_t index) const;
	int getCustomTextureId(size_t index) const;
};
