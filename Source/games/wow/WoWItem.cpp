#include "WoWItem.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

#include "Attachment.h"
#include "database.h" // items
#include "Game.h"
#include "RaceInfos.h"
#include "wow_enums.h"
#include "WoWModel.h"

#include "logger/Logger.h"
#include "string_utils.h"

std::map<CharSlots, int> WoWItem::SLOT_LAYERS_ =
{
	{CS_SHIRT, 10}, {CS_HEAD, 11}, {CS_SHOULDER, 13},
	{CS_PANTS, 10}, {CS_BOOTS, 11}, {CS_CHEST, 13},
	{CS_TABARD, 17}, {CS_BELT, 18}, {CS_BRACERS, 19},
	{CS_GLOVES, 20}, {CS_HAND_RIGHT, 21}, {CS_HAND_LEFT, 22},
	{CS_CAPE, 23}, {CS_QUIVER, 24}
};

WoWItem::WoWItem(CharSlots slot) : slot_(slot)
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

		const auto itemlevels = GAMEDATABASE.sqlQuery(
			std::format(
				"SELECT OrderIndex, ItemAppearanceID, ItemAppearanceModifierID FROM ItemModifiedAppearance WHERE ItemID = {}",
				id));

		if (itemlevels.valid && !itemlevels.values.empty())
		{
			nbLevels_ = 0;
			level_ = 0;
			levelDisplayMap_.clear();
			for (auto& value : itemlevels.values)
			{
				if (value[1].empty() || value[2].empty())
						continue;
					const auto curid = core::safeStoi(value[1]);
					modifierIdDisplayMap_[core::safeStoi(value[2])] = curid;
				// if display id is null (case when item's look doesn't change with level)
				if (curid == 0)
					continue;

				//check if display id already in the map (do not duplicate when look is the same)
				auto found = false;
				for (const auto& it : levelDisplayMap_)
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

		if (levelDisplayMap_.count(level_))
		{
			const auto iteminfos = GAMEDATABASE.sqlQuery(std::format("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = {}",
				levelDisplayMap_[level_]));

			if (iteminfos.valid && !iteminfos.values.empty() && !iteminfos.values[0][0].empty())
					displayId_ = core::safeStoi(iteminfos.values[0][0]);
			}

			const auto& itemRcd = items.getById(id);
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

		const auto iteminfos = GAMEDATABASE.sqlQuery(std::format("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = {}",
			levelDisplayMap_[level_]));

		if (iteminfos.valid && !iteminfos.values.empty())
			displayId_ = core::safeStoi(iteminfos.values[0][0]);

		const auto& itemRcd = items.getById(id_);
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
		const auto iteminfos = GAMEDATABASE.sqlQuery(std::format("SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = {}",
			it->second));

		if (iteminfos.valid && !iteminfos.values.empty())
			displayId_ = core::safeStoi(iteminfos.values[0][0]);

		const auto& itemRcd = items.getById(id_);
		setName(itemRcd.name);
		quality_ = itemRcd.quality;
		type_ = itemRcd.type;
		load();
	}
}

void WoWItem::onParentSet(Component* parent)
{
	charModel_ = dynamic_cast<WoWModel*>(parent);
}

void WoWItem::unload()
{
	// delete models and clear map
	for (const auto& itemModel : itemModels_)
		delete itemModel.second;

	itemModels_.clear();

	// release textures and clear map
	for (const auto& itemTexture : itemTextures_)
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
		delete m;
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
	if (!queryItemInfo(
		std::format("SELECT GeoSetGroup1, GeoSetGroup2, GeoSetGroup3, GeoSetGroup4, GeoSetGroup5, GeoSetGroup6, "
			"AttachmentGeoSetGroup1, AttachmentGeoSetGroup2, AttachmentGeoSetGroup3, "
			"AttachmentGeoSetGroup4, AttachmentGeoSetGroup5, AttachmentGeoSetGroup6, Flags "
			"FROM ItemDisplayInfo WHERE ItemDisplayInfo.ID = {}", displayId_),
		iteminfos))
		return;

	const int geosetGroup[6] = {
		core::safeStoi(iteminfos.values[0][0]), core::safeStoi(iteminfos.values[0][1]),
		core::safeStoi(iteminfos.values[0][2]), core::safeStoi(iteminfos.values[0][3]),
		core::safeStoi(iteminfos.values[0][4]), core::safeStoi(iteminfos.values[0][5])
	};

	const int attachmentGeosetGroup[6] =
	{
		core::safeStoi(iteminfos.values[0][6]), core::safeStoi(iteminfos.values[0][7]),
		core::safeStoi(iteminfos.values[0][8]), core::safeStoi(iteminfos.values[0][9]),
		core::safeStoi(iteminfos.values[0][10]), core::safeStoi(iteminfos.values[0][11])
	};

	displayFlags_ = core::safeStoi(iteminfos.values[0][12]);

	// query models
	const int models[2] = {getCustomModelId(0), getCustomModelId(1)};

	// query textures
	const int textures[2] = {getCustomTextureId(0), getCustomTextureId(1)};

	// query textures from ItemDisplayInfoMaterialRes (if relevant)
	auto texinfos = GAMEDATABASE.sqlQuery(
		std::format("SELECT * FROM ItemDisplayInfoMaterialRes WHERE ItemDisplayInfoID = {}", displayId_));
	if (texinfos.valid && !texinfos.empty())
	{
		auto classFilter = std::format("ComponentTextureFileData.ClassID = {}", static_cast<int>(CLASS_ANY));
		if (charModel_ && charModel_->cd.isDemonHunter())
			classFilter = std::format("(ComponentTextureFileData.ClassID = {} OR ComponentTextureFileData.ClassID = {})",
						  static_cast<int>(CLASS_DEMONHUNTER), static_cast<int>(CLASS_ANY));

		if (queryItemInfo(std::format("SELECT FileDataID FROM ItemDisplayInfoMaterialRes "
							  "LEFT JOIN TextureFileData ON ItemDisplayInfoMaterialRes.MaterialResourcesID = TextureFileData.MaterialResourcesID "
							  "INNER JOIN ComponentTextureFileData ON ComponentTextureFileData.ID = TextureFileData.FileDataID "
							  "WHERE (ComponentTextureFileData.GenderIndex = {} OR ComponentTextureFileData.GenderIndex = {}) "
							  "AND ItemDisplayInfoID = {} AND {} "
							  "ORDER BY ComponentTextureFileData.GenderIndex, ComponentTextureFileData.ClassID DESC",
						  static_cast<int>(GENDER_ANY), charInfos.sexID, displayId_, classFilter),
						  iteminfos))
		{
			for (auto& value : iteminfos.values)
			{
				const auto tex = GAMEDIRECTORY.getFile(core::safeStoi(value[0]));
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
			const auto query = std::format("SELECT ID, PositionIndex FROM ComponentModelFileData "
				"WHERE ID IN ({},{})", models[0], models[1]);

			const auto result = GAMEDATABASE.sqlQuery(query);

			auto leftIndex = 0;
			auto rightIndex = 1;
			if (result.valid && !result.values.empty())
			{
				const auto modelid = core::safeStoi(result.values[0][0]);
				const auto position = core::safeStoi(result.values[0][1]);

				// If the modelid matches models[0], use position to determine left/right
				// Otherwise, swap the indices
				if ((modelid == models[0] && position != 0) || (modelid != models[0] && position == 0))
				{
					leftIndex = 1;
					rightIndex = 0;
				}
			}
			else
			{
				LOG_ERROR << "Impossible to query information for item" << name() << "(id " << id_ << "- display id" <<
					displayId_ << ") - SQL ERROR";
				LOG_ERROR << query.c_str();
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
			auto* tex = GAMEDIRECTORY.getFile(textures[0]);
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

				auto* texture = charModel_->td.GetBackgroundTex(CR_TORSO_UPPER);
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
			(slot_ != CS_HAND_LEFT)) // treat left hand attachment in a special case - cf CS_HAND_LEFT
		{
			charModel_->attachment->delSlot(slot_);
			for (const auto it : itemModels_)
				charModel_->attachment->addChild(it.second, it.first, slot_);
		}
	}

	// add textures if any
	if ((slot_ != CS_BOOTS) && // treat boots texturing in a special case - cf CS_BOOTS
		(slot_ != CS_GLOVES) && // treat gloves texturing in a special case - cf CS_GLOVES 
		(slot_ != CS_TABARD) && // treat tabard texturing in a special case - cf CS_TABARD 
		(slot_ != CS_CAPE)) // treat cape texturing in a special case - cf CS_CAPE 
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
					const auto& item = items.getById(id_);
					if (charModel_->bSheathe && item.sheath != SHEATHETYPE_NONE)
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
					const auto& item = items.getById(id_);
					int attachement = ATT_LEFT_PALM;

					if (item.type == IT_SHIELD)
						attachement = ATT_LEFT_WRIST;

					if (charModel_->bSheathe && item.sheath != SHEATHETYPE_NONE)
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
				if (!charModel_->isWearingARobe())
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
	case CS_CHEST: // nothing specific for shirt & chest items
			break;
	case CS_BRACERS: // nothing specific for bracers items
			break;
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
				static const std::pair<CharRegions, CharRegions> tabardLayers[] = {
					{CR_TABARD_1, CR_TORSO_UPPER},
					{CR_TABARD_2, CR_TORSO_LOWER},
					{CR_TABARD_3, CR_TORSO_UPPER},
					{CR_TABARD_4, CR_TORSO_LOWER},
					{CR_TABARD_5, CR_TORSO_UPPER},
					{CR_TABARD_6, CR_TORSO_LOWER}
				};
				for (const auto& layer : tabardLayers)
				{
					auto it = itemTextures_.find(layer.first);
					if (it != itemTextures_.end())
						charModel_->tex.addLayer(it->second, layer.second, SLOT_LAYERS_[slot_]);
				}
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
		id_ == 69210); // Renowned Guild Tabard
}

void WoWItem::save(pugi::xml_node& parentNode) const
{
	pugi::xml_node node = parentNode.append_child("item");

	node.append_child("slot").append_attribute("value") = static_cast<int>(slot_);
	node.append_child("id").append_attribute("value") = id_;
	node.append_child("displayId").append_attribute("value") = displayId_;
	node.append_child("level").append_attribute("value") = level_;

	if (isCustomizableTabard())
		charModel_->td.save(node);
}

void WoWItem::load(const std::string& f)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(f.c_str());
	if (!result)
	{
		LOG_ERROR << "Fail to open" << f.c_str();
		return;
	}

	// Find all item nodes and look for the matching slot
	pugi::xml_node root = doc.document_element();
	for (pugi::xml_node itemNode = root.child("item"); itemNode; itemNode = itemNode.next_sibling("item"))
	{
		pugi::xml_node slotNode = itemNode.child("slot");
		if (!slotNode)
			continue;

		const auto slot = slotNode.attribute("value").as_uint();
		if (slot != static_cast<unsigned int>(slot_))
			continue;

		pugi::xml_node idNode = itemNode.child("id");
		if (idNode)
		{
			const auto id = idNode.attribute("value").as_int();
			if (id != -1)
				setId(id);
		}

		pugi::xml_node displayIdNode = itemNode.child("displayId");
		if (displayIdNode)
		{
			const auto id = displayIdNode.attribute("value").as_int();
			if (id_ == -1)
				setDisplayId(id);
		}

		pugi::xml_node levelNode = itemNode.child("level");
		if (levelNode)
		{
			const auto level = levelNode.attribute("value").as_int();
			setLevel(level);
		}

		if (isCustomizableTabard())
		{
			pugi::xml_node tabardNode = itemNode.child("TabardDetails");
			if (tabardNode)
			{
				charModel_->td.load(tabardNode);
				load(); // refresh tabard textures
			}
		}

		break; // found matching slot
	}
}

void WoWItem::updateItemModel(POSITION_SLOTS pos, int modelId, int textureId)
{
	if (modelId == 0)
		return;

	auto* m = new WoWModel(GAMEDIRECTORY.getFile(modelId), true);

	if (m->ok)
	{
		for (uint i = 0; i < m->geosets.size(); i++)
			m->showGeoset(i, true);

		itemModels_[pos] = m;
		auto* texture = GAMEDIRECTORY.getFile(textureId);
		if (texture)
			m->updateTextureList(texture, TEXTURE_OBJECT_SKIN);
		else
			LOG_ERROR << "Error during item update" << id_ << "(display id" << displayId_ << "). Texture" << textureId
				<< "can't be loaded";
	}
	else
	{
		LOG_ERROR << "Error during item update" << id_ << "(display id" << displayId_ << "). Model" << modelId <<
			"can't be loaded";
	}
}

void WoWItem::mergeModel(CharSlots slot, int modelId, int textureId)
{
	if (modelId == 0)
		return;

	mergedModel_ = new WoWModel(GAMEDIRECTORY.getFile(modelId), true);

	if (mergedModel_->ok)
	{
		auto* texture = GAMEDIRECTORY.getFile(textureId);
		if (texture)
		{
			mergedModel_->updateTextureList(texture, TEXTURE_OBJECT_SKIN);
			charModel_->updateTextureList(texture, TEXTURE_OBJECT_SKIN);
		}
		else
			LOG_ERROR << "Error during item update" << id_ << "(display id" << displayId_ << "). Texture" << textureId
				<< "can't be loaded";

		for (uint i = 0; i < mergedModel_->geosets.size(); i++)
			mergedModel_->hideAllGeosets();
	}
	else
	{
		LOG_ERROR << "Error during item update" << id_ << "(display id" << displayId_ << "). Model" << modelId <<
			"can't be loaded";
	}
}

CharRegions WoWItem::getRegionForTexture(GameFile* file) const
{
	auto result = CR_UNK8;

	if (file)
	{
		std::string fullname = file->fullname();
		std::transform(fullname.begin(), fullname.end(), fullname.begin(),
			[](unsigned char c) { return std::tolower(c); });

		if (fullname.find("armlowertexture") != std::string::npos)
		{
			result = CR_ARM_LOWER;
		}
		else if (fullname.find("armuppertexture") != std::string::npos)
		{
			result = CR_ARM_UPPER;
		}
		else if (fullname.find("foottexture") != std::string::npos)
		{
			result = CR_FOOT;
		}
		else if (fullname.find("handtexture") != std::string::npos)
		{
			result = CR_HAND;
		}
		else if (fullname.find("leglowertexture") != std::string::npos)
		{
			result = CR_LEG_LOWER;
		}
		else if (fullname.find("leguppertexture") != std::string::npos)
		{
			result = CR_LEG_UPPER;
		}
		else if (fullname.find("torsolowertexture") != std::string::npos)
		{
			result = CR_TORSO_LOWER;
		}
		else if (fullname.find("torsouppertexture") != std::string::npos)
		{
			result = CR_TORSO_UPPER;
		}
		else if (fullname.find("cape") != std::string::npos)
		{
			result = CR_CAPE;
		}
		else
		{
			LOG_ERROR << "Unable to determine region for texture" << file->fullname().c_str() << " - item" << id_ << "displayid" <<
				displayId_;
		}
	}

	return result;
}

bool WoWItem::queryItemInfo(const std::string& query, sqlResult& result) const
{
	result = GAMEDATABASE.sqlQuery(query);

	if (!result.valid || result.values.empty())
	{
		LOG_ERROR << "Impossible to query information for item" << name() << "(id " << id_ << "- display id" <<
			displayId_ << ") - SQL ERROR";
		LOG_ERROR << query.c_str();
		return false;
	}

	return true;
}

int WoWItem::getCustomModelId(size_t index) const
{
	if (!charModel_)
		return 0;

	sqlResult infos;
	if (!queryItemInfo(std::format("SELECT FileDataID FROM ItemDisplayInfo "
						   "LEFT JOIN ModelFileData ON {} = ModelFileData.ModelResourcesID "
						   "WHERE ItemDisplayInfo.ID = {}",
						   (index == 0) ? "ModelResourcesID1" : "ModelResourcesID2", displayId_),
					   infos))
		return 0;

	// if there is only one result, return model id:
	if (infos.values.size() == 1)
		return core::safeStoi(infos.values[0][0]);

	// if there are multiple values, check by race and sex:
	std::string idListStr = "(";
	for (size_t i = 0; i < infos.values.size(); i++)
	{
		if (i > 0) idListStr += ", ";
		idListStr += infos.values[i][0];
	}
	idListStr += ")";

	const auto charInfos = charModel_->infos;

	auto classFilter = std::format("ClassID = {}", static_cast<int>(CLASS_ANY));
	if (charModel_->cd.isDemonHunter())
		classFilter = std::format("(ClassID = {} OR ClassID = {})", static_cast<int>(CLASS_DEMONHUNTER), static_cast<int>(CLASS_ANY));

	// It looks like shoulders are always in pairs, with PositionIndex values 0 and 1.
	// Depending on index (model 1 or 2) we sort the PositionIndex differently so one will
	// return left and one right shoulder. Noting this in case in the future it turns out
	// this assumption isn't always right - Wain
	const std::string positionSort = ((index == 0) ? "" : "DESC");

	// Order all queries by GenderIndex to ensure definite genders have priority over generic ones,
	// and ClassID descending to ensure Demon Hunter textures have priority over regular ones, for DHs only:
	sqlResult iteminfos;
	std::string query = std::format("SELECT ID, PositionIndex FROM ComponentModelFileData "
					"WHERE RaceID = {} AND (GenderIndex = {} OR GenderIndex = {}) "
					"AND ID IN {} AND {} "
					"ORDER BY GenderIndex, ClassID DESC, PositionIndex {}",
					charInfos.raceID, charInfos.sexID, static_cast<int>(GENDER_ANY), idListStr, classFilter,
					positionSort);
	if (queryItemInfo(query, iteminfos))
		return core::safeStoi(iteminfos.values[0][0]);

	// Failed to find model for that specific race and sex, so check fallback race:
	if (charInfos.modelFallbackRaceID > 0)
	{
		query = std::format("SELECT ID, PositionIndex FROM ComponentModelFileData "
					"WHERE RaceID = {} AND (GenderIndex = {} OR GenderIndex = {}) "
					"AND ID IN {} AND {} "
					"ORDER BY GenderIndex, ClassID DESC, PositionIndex {}",
					charInfos.modelFallbackRaceID, charInfos.modelFallbackSexID, static_cast<int>(GENDER_NONE), idListStr,
					classFilter, positionSort);

		if (queryItemInfo(query, iteminfos))
			return core::safeStoi(iteminfos.values[0][0]);
	}

	// We still didn't find the model, so check for RACE_ANY (race = 0) items:
	// Note: currently all race = 0 entries are also gender = 2, but we probably
	// shouldn't assume it will stay that way, so check for both gender values:
	query = std::format("SELECT ID, PositionIndex FROM ComponentModelFileData "
				"WHERE RaceID = {} AND (GenderIndex = {} OR GenderIndex = {}) "
				"AND ID IN {} AND {} "
				"ORDER BY GenderIndex, ClassID DESC, PositionIndex {}",
				static_cast<int>(RACE_ANY), charInfos.modelFallbackSexID, static_cast<int>(GENDER_NONE), idListStr, classFilter,
				positionSort);

	if (queryItemInfo(query, iteminfos))
		return core::safeStoi(iteminfos.values[0][0]);

	return 0;
}

int WoWItem::getCustomTextureId(size_t index) const
{
	if (!charModel_)
		return 0;

	sqlResult infos;
	if (!queryItemInfo(std::format("SELECT FileDataID FROM ItemDisplayInfo "
						   "LEFT JOIN TextureFileData ON {} = TextureFileData.MaterialResourcesID "
						   "WHERE ItemDisplayInfo.ID = {}",
						   (index == 0) ? "ModelMaterialResourcesID1" : "ModelMaterialResourcesID2", displayId_),
					   infos))
		return 0;

	// if there is only one result, return texture id:
	if (infos.values.size() == 1)
		return core::safeStoi(infos.values[0][0]);

	// if there are multiple values, check by race and sex:
	std::string idListStr = "(";
	for (size_t i = 0; i < infos.values.size(); i++)
	{
		if (i > 0) idListStr += ", ";
		idListStr += infos.values[i][0];
	}
	idListStr += ")";

	const auto charInfos = charModel_->infos;

	std::string classFilter = std::format("ClassID = {}", static_cast<int>(CLASS_ANY));

	if (charModel_->cd.isDemonHunter())
		classFilter = std::format("(ClassID = {} OR ClassID = {})", static_cast<int>(CLASS_DEMONHUNTER), static_cast<int>(CLASS_ANY));

	// Order all queries by GenderIndex to ensure definite genders have priority over generic ones,
	// and ClassID descending to ensure Demon Hunter textures have priority over regular ones, for DHs only:
	sqlResult iteminfos;
	std::string query = std::format("SELECT ID FROM ComponentTextureFileData "
					"WHERE RaceID = {} AND (GenderIndex = {} OR GenderIndex = {}) "
					"AND ID IN {} AND {} "
					"ORDER BY GenderIndex, ClassID DESC",
					charInfos.raceID, charInfos.sexID, static_cast<int>(GENDER_ANY), idListStr, classFilter);
	if (queryItemInfo(query, iteminfos))
		return core::safeStoi(iteminfos.values[0][0]);

	// Failed to find model for that specific race and sex, so check fallback race:
	if (charInfos.textureFallbackRaceID > 0)
	{
		query = std::format("SELECT ID FROM ComponentTextureFileData "
					"WHERE RaceID = {} AND (GenderIndex = {} OR GenderIndex = {}) "
					"AND ID IN {} AND {} "
					"ORDER BY GenderIndex, ClassID DESC",
					charInfos.textureFallbackRaceID, charInfos.textureFallbackSexID, static_cast<int>(GENDER_ANY), idListStr,
					classFilter);

		if (queryItemInfo(query, iteminfos))
			return core::safeStoi(iteminfos.values[0][0]);
	}

	// We still didn't find the model, so check for RACE_ANY (race = 0) items: 
	query = std::format("SELECT ID FROM ComponentTextureFileData "
				"WHERE RaceID = {} AND (GenderIndex = {} OR GenderIndex = {}) "
				"AND ID IN {} AND {} "
				"ORDER BY GenderIndex, ClassID DESC",
				static_cast<int>(RACE_ANY), charInfos.sexID, static_cast<int>(GENDER_ANY), idListStr, classFilter);

	if (queryItemInfo(query, iteminfos))
		return core::safeStoi(iteminfos.values[0][0]);

	return 0;
}
