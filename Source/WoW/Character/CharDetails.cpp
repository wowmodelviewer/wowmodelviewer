#include "CharDetails.h"

#include "CharDetailsEvent.h"
#include "DB2Table.h"
#include "Game.h"
#include "WoWModel.h"
#include "Logger.h"
#include "string_utils.h"

#include <set>

CharDetails::CharDetails():
	eyeGlowType(EGT_NONE), showUnderwear(true), showEars(true), showHair(true),
	showFacialHair(true), showFeet(true), autoHideGeosetsForHeadItems(true),
	isNPC(true), model_(nullptr), isDemonHunter_(false)
{
	refreshGeosets();
}

void CharDetails::save(pugi::xml_node& parentNode)
{
	pugi::xml_node node = parentNode.append_child("CharDetails");

	for (const auto& opt : currentCustomization_)
	{
		pugi::xml_node child = node.append_child("customization");
		child.append_attribute("id") = opt.first;
		child.append_attribute("value") = opt.second;
	}

	node.append_child("eyeGlowType").append_attribute("value") = static_cast<int>(eyeGlowType);
	node.append_child("showUnderwear").append_attribute("value") = showUnderwear ? 1 : 0;
	node.append_child("showEars").append_attribute("value") = showEars ? 1 : 0;
	node.append_child("showHair").append_attribute("value") = showHair ? 1 : 0;
	node.append_child("showFacialHair").append_attribute("value") = showFacialHair ? 1 : 0;
	node.append_child("showFeet").append_attribute("value") = showFeet ? 1 : 0;
	node.append_child("isDemonHunter").append_attribute("value") = isDemonHunter_ ? 1 : 0;
}

void CharDetails::load(const std::string& f)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(f.c_str());
	if (!result)
	{
		LOG_ERROR << "Fail to open" << f.c_str();
		return;
	}

	pugi::xml_node charNode = doc.document_element();
	// Navigate to CharDetails node if needed
	if (std::string(charNode.name()) != "CharDetails")
		charNode = charNode.child("CharDetails");

	if (!charNode)
		charNode = doc.child("model").child("CharDetails");

	if (!charNode)
	{
		LOG_ERROR << "CharDetails node not found in" << f.c_str();
		return;
	}

	for (pugi::xml_node child = charNode.first_child(); child; child = child.next_sibling())
	{
		const std::string name = child.name();

		if (name == "customization")
			set(child.attribute("id").as_uint(), child.attribute("value").as_uint());
		else if (name == "eyeGlowType")
			eyeGlowType = static_cast<EyeGlowTypes>(child.attribute("value").as_uint());
		else if (name == "showUnderwear")
			showUnderwear = child.attribute("value").as_uint();
		else if (name == "showEars")
			showEars = child.attribute("value").as_uint();
		else if (name == "showHair")
			showHair = child.attribute("value").as_uint();
		else if (name == "showFacialHair")
			showFacialHair = child.attribute("value").as_uint();
		else if (name == "showFeet")
			showFeet = child.attribute("value").as_uint();
		else if (name == "isDemonHunter")
		{
			LOG_INFO << __FILE__ << __LINE__ << "reading demonHunter mode value";
			setDemonHunterMode(child.attribute("value").as_uint());
		}
	}
}

void CharDetails::reset(WoWModel* model)
{
	if ((model != nullptr) & (model != model_))
	{
		model_ = model;
		fillCustomizationMap();
	}

	currentCustomization_.clear();

	showUnderwear = true;
	showHair = true;
	showFacialHair = true;
	showEars = true;
	showFeet = false;

	isNPC = false;

	// Auto-enable demon hunter mode for Night Elf and Blood Elf races
	isDemonHunter_ = model_ && (model_->infos.raceID == RACE_NIGHTELF || model_->infos.raceID == RACE_BLOODELF);

	// Set default options to their first available choice (only options without Flags & 0x20)
	for (const auto& c : choicesPerOptionMap_)
	{
		if (!c.second.empty() && defaultOptionIds_.count(c.first))
			currentCustomization_[c.first] = c.second[0];
	}

	// Single rebuild of all customization elements
	rebuildAllCustomizationElements();

	refreshGeosets();
	refreshTextures();

	// Notify observers for all options
	for (const auto& c : choicesPerOptionMap_)
	{
		if (!c.second.empty())
		{
			CharDetailsEvent event(this, CharDetailsEvent::CHOICE_LIST_CHANGED);
			event.setCustomizationOptionId(c.first);
			notify(event);
		}
	}

	if (model_)
		model_->refresh();
}

void CharDetails::randomise()
{
	reset();
}

void CharDetails::fillCustomizationMap()
{
	if (!model_)
		return;

	// clear any previous value found
	choicesPerOptionMap_.clear();
	defaultOptionIds_.clear();

	const auto infos = model_->infos;
	if (infos.raceID == -1)
		return;

	const auto options = WOWDB.getTable("ChrCustomizationOption");

	if (options)
	{
		// Collect matching options and sort by OrderIndex
		struct OptionEntry { uint id; uint orderIndex; uint flags; };
		std::vector<OptionEntry> matchingOptions;
		for (const auto& row : *options)
		{
			if (row.getUInt("ChrModelID") == static_cast<uint32_t>(infos.ChrModelID[0]))
			{
				matchingOptions.push_back({static_cast<uint>(row.recordID()), row.getUInt("OrderIndex"), row.getUInt("Flags")});
			}
		}
		std::sort(matchingOptions.begin(), matchingOptions.end(),
			[](const OptionEntry& a, const OptionEntry& b) { return a.orderIndex < b.orderIndex; });

		for (const auto& opt : matchingOptions)
		{
			choicesPerOptionMap_[opt.id] = {};

			// Options without flag 0x20 get a default choice on reset (matches wow.export)
			if (!(opt.flags & 0x20))
				defaultOptionIds_.insert(opt.id);
		}
	}

	for (const auto& option : choicesPerOptionMap_)
		fillCustomizationMapForOption(option.first);
}

void CharDetails::fillCustomizationMapForOption(uint chrCustomizationOption)
{
	auto& vals = choicesPerOptionMap_[chrCustomizationOption];
	const auto originalVals = std::move(vals);
	vals.clear();

	// 1. fill direct values
	const DB2Table* choicesTbl = WOWDB.getTable("ChrCustomizationChoice");
	if (choicesTbl)
	{
		struct ChoiceEntry { uint id; uint orderIndex; };
		std::vector<ChoiceEntry> matchingChoices;
		for (const auto& row : *choicesTbl)
		{
			if (row.getUInt("ChrCustomizationOptionID") == chrCustomizationOption)
				matchingChoices.push_back({static_cast<uint>(row.recordID()), row.getUInt("OrderIndex")});
		}
		std::sort(matchingChoices.begin(), matchingChoices.end(),
			[](const ChoiceEntry& a, const ChoiceEntry& b) { return a.orderIndex < b.orderIndex; });

		LOG_INFO << __FUNCTION__ << "DIRECT values" << matchingChoices.size();
		for (const auto& c : matchingChoices)
			vals.push_back(c.id);
	}

	if (vals != originalVals)
	{
		LOG_INFO << __FUNCTION__ << chrCustomizationOption;
		std::string info;
		for (const auto& v : vals)
			info += std::to_string(v) + " ";
		LOG_INFO << info;

		CharDetailsEvent event(this, CharDetailsEvent::CHOICE_LIST_CHANGED);
		event.setCustomizationOptionId(chrCustomizationOption);
		notify(event);
	}
}

void CharDetails::set(uint chrCustomizationOptionID, uint chrCustomizationChoiceID) // wow version >= 9.x
{
	if (!model_)
		return;

	const auto infos = model_->infos;
	if (infos.raceID == -1)
		return;

	currentCustomization_[chrCustomizationOptionID] = chrCustomizationChoiceID;

	LOG_INFO << __FUNCTION__ << chrCustomizationOptionID << chrCustomizationChoiceID;

	// Full rebuild of all customization elements using the active choice set
	rebuildAllCustomizationElements();

	CharDetailsEvent event(this, CharDetailsEvent::CHOICE_LIST_CHANGED);
	event.setCustomizationOptionId(chrCustomizationOptionID);
	notify(event);

	model_->refresh();
	// TEXTUREMANAGER.dump();
}

std::vector<uint> CharDetails::getCustomizationChoices(const uint chrCustomizationOptionID)
{
	if (choicesPerOptionMap_.count(chrCustomizationOptionID) == 0)
		fillCustomizationMap();

	auto it = choicesPerOptionMap_.find(chrCustomizationOptionID);
	if (it != choicesPerOptionMap_.end())
		return it->second;
	return {};
}

uint CharDetails::get(uint chrCustomizationOptionID) const
{
	auto it = currentCustomization_.find(chrCustomizationOptionID);
	if (it != currentCustomization_.end())
		return it->second;
	return 0;
}

void CharDetails::rebuildAllCustomizationElements()
{
	customizationElementsPerOption_.clear();

	const DB2Table* elementsTbl = WOWDB.getTable("ChrCustomizationElement");
	if (!elementsTbl)
		return;

	// Build set of active choice IDs for fast lookup
	std::set<uint> activeChoiceIds;
	for (const auto& [optionId, choiceId] : currentCustomization_)
		activeChoiceIds.insert(choiceId);

	const DB2Table* geosetTbl = WOWDB.getTable("ChrCustomizationGeoset");
	const DB2Table* skinnedTbl = WOWDB.getTable("ChrCustomizationSkinnedModel");
	const DB2Table* matTbl = WOWDB.getTable("ChrCustomizationMaterial");
	const DB2Table* texFileTbl = WOWDB.getTable("TextureFileData");
	const DB2Table* layerTbl = WOWDB.getTable("ChrModelTextureLayer");

	// For each active choice, find and apply matching elements
	for (const auto& [optionId, choiceId] : currentCustomization_)
	{
		for (const auto& row : *elementsTbl)
		{
			if (row.getUInt("ChrCustomizationChoiceID") != choiceId)
				continue;

			const uint eltId = static_cast<uint>(row.recordID());
			const uint geosetID = row.getUInt("ChrCustomizationGeosetID");
			const uint skinnedModelID = row.getUInt("ChrCustomizationSkinnedModelID");
			const uint materialID = row.getUInt("ChrCustomizationMaterialID");

			// Geosets: apply regardless of RelatedChrCustomizationChoiceID (matches wow.export)
			if (geosetID != 0 && geosetTbl)
			{
				LOG_INFO << "ChrCustomizationGeosetID based customization for" << eltId << "/" << geosetID;
				DB2Row geoRow = geosetTbl->getRow(geosetID);
				if (geoRow)
				{
					customizationElementsPerOption_[optionId].geosets.emplace_back(
						geoRow.getUInt("GeosetType"), geoRow.getUInt("GeosetID"));
				}
			}

			// Skinned models: apply regardless of RelatedChrCustomizationChoiceID
			if (skinnedModelID != 0 && skinnedTbl)
			{
				LOG_INFO << "ChrCustomizationSkinnedModelID based customization for" << eltId << "/" << skinnedModelID;
				DB2Row skinRow = skinnedTbl->getRow(skinnedModelID);
				if (skinRow)
				{
					customizationElementsPerOption_[optionId].models.emplace_back(
						skinRow.getUInt("CollectionsFileDataID"),
						std::make_pair(skinRow.getUInt("GeosetType"), skinRow.getUInt("GeosetID")));
				}
			}

			// Materials: only apply if RelatedChrCustomizationChoiceID is 0 or in the active set
			if (materialID != 0 && matTbl && texFileTbl && layerTbl)
			{
				const uint relatedChoiceId = row.getUInt("RelatedChrCustomizationChoiceID");
				if (relatedChoiceId != 0 && activeChoiceIds.find(relatedChoiceId) == activeChoiceIds.end())
					continue;

				LOG_INFO << "ChrCustomizationMaterialID based customization for" << eltId << "/" << materialID;

				DB2Row matRow = matTbl->getRow(materialID);
				if (matRow)
				{
					const uint32_t materialResID = matRow.getUInt("MaterialResourcesID");
					const uint32_t textureTargetID = matRow.getUInt("ChrModelTextureTargetID");

					// Find FileDataID from TextureFileData
					uint fileDataID = 0;
					for (const auto& tfdRow : *texFileTbl)
					{
						if (tfdRow.getUInt("MaterialResourcesID") == materialResID)
						{
							fileDataID = tfdRow.getUInt("FileDataID");
							break;
						}
					}

					// Find ChrModelTextureLayer matching target and layout
					uint layer = 0;
					int bitmask = -1;
					uint textureType = 0;
					uint blendMode = 0;
					for (const auto& layerRow : *layerTbl)
					{
						if (layerRow.getUInt("ChrModelTextureTargetID1") == textureTargetID &&
							static_cast<int>(layerRow.getUInt("CharComponentTextureLayoutsID")) == model_->infos.textureLayoutID)
						{
							layer = layerRow.getUInt("Layer");
							bitmask = layerRow.getInt("TextureSectionTypeBitMask");
							textureType = layerRow.getUInt("TextureType");
							blendMode = layerRow.getUInt("BlendMode");
							break;
						}
					}

					TextureCustomization t{};
					t.layer = layer;
					t.region = bitMaskToSectionType(bitmask);
					t.type = textureType;
					t.blendMode = blendMode;
					t.fileId = fileDataID;

					LOG_INFO << "texture ->" << "layer" << t.layer << "region" << t.region << "type" << t.type <<
						"blendMode" << t.blendMode << "fileId" << t.fileId;

					customizationElementsPerOption_[optionId].textures.push_back(t);
				}
			}
		}
	}
}

int CharDetails::bitMaskToSectionType(int mask)
{
	if (mask == -1)
		return -1;

	if (mask == 0)
		return 0;

	auto val = 1;

	while (((mask = mask >> 1) & 0x01) == 0)
		val++;

	return val;
}

void CharDetails::refresh()
{
	refreshGeosets();
	refreshTextures();
	refreshSkinnedModels();
}


void CharDetails::refreshGeosets()
{
	geosets.clear();

	for (auto i = 0; i < NUM_GEOSETS; i++)
		geosets[i] = 1;

	// Eyeglow and Earrings/Piercings should be hidden by default (matches wow.export)
	geosets[CG_EYEGLOW] = 0;
	geosets[CG_EARRINGS] = 0;

	if (showEars)
		geosets[CG_EARS] = 2;
	else
		geosets[CG_EARS] = 0;

	// apply customization elements
	for (const auto& elt : customizationElementsPerOption_)
	{
		for (auto geo : elt.second.geosets)
		{
			// don't display ears if option is unchecked
			if (geo.first == CG_EARS && !showEars)
				continue;

			// don't display hair if option is unchecked
			if (geo.first == CG_SKIN_OR_HAIR && !showHair)
				continue;

			// don't display facial hairs if option is unchecked
			if ((geo.first == CG_FACE_1 || geo.first == CG_FACE_2 || geo.first == CG_FACE_3) && !showFacialHair)
				continue;

			geosets[geo.first] = geo.second;
		}
	}

	if (model_)
	{
		// only show underwear bottoms if the character isn't wearing pants or chest 
		if (showUnderwear && model_->getItemId(CS_PANTS) < 1 && !model_->isWearingARobe())
		{
			// demon hunters and female pandaren use the TABARD2 geoset for part of their underwear:
			if (isDemonHunter_ || ((model_->infos.raceID == RACE_PANDAREN) && (model_->infos.sexID == GENDER_FEMALE)))
				geosets[CG_DH_LOINCLOTH] = 1;
		}
		else // hide underwear
		{
			// demon hunters and female pandaren - need to hide the TABARD2 geoset when no underwear:
			if (isDemonHunter_ || ((model_->infos.raceID == RACE_PANDAREN) && (model_->infos.sexID == GENDER_FEMALE)))
				geosets[CG_DH_LOINCLOTH] = 0;
		}
	}
}

void CharDetails::refreshTextures()
{
	textures.clear();

	// apply customization elements
	for (const auto& elt : customizationElementsPerOption_)
	{
		for (auto t : elt.second.textures)
		{
			if (model_ != nullptr)
			{
				// don't apply underwear tops/bras if show underwear is off or if the character is wearing a shirt or chest
				if (t.region == CR_TORSO_UPPER &&
					(!showUnderwear ||
						model_->getItemId(CS_CHEST) > 1 || model_->getItemId(CS_SHIRT) > 1))
					continue;

				// don't apply underwear bottoms if show underwear is off or if the character is wearing pants
				if (t.region == CR_LEG_UPPER &&
					(!showUnderwear ||
						model_->getItemId(CS_PANTS) > 1))
					continue;
			}

			textures.push_back(t);
		}
	}
}

void CharDetails::refreshSkinnedModels()
{
	// first clean any previous merging
	for (const auto& m : models_)
		model_->unmergeModel(m.first);

	models_.clear();

	for (const auto& elt : customizationElementsPerOption_)
	{
		for (const auto m : elt.second.models)
		{
			auto* model = model_->mergeModel(m.first);
			model->setGeosetGroupDisplay(static_cast<CharGeosets>(m.second.first), m.second.second);
			models_.emplace_back(m.first, m.second);
		}
	}
}
