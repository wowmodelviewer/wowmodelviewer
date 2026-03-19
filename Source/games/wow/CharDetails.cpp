#include "CharDetails.h"

#include "CharDetailsEvent.h"
#include "DB2Table.h"
#include "Game.h"
#include "WoWModel.h"
#include "logger/Logger.h"
#include "string_utils.h"

#include <set>

std::multimap<uint, int> CharDetails::LINKED_OPTIONS_MAP_ =
{
	// hardcoded values (need to figure out how to find this from DB - if possible ?)
	{726, 724}, // veins color linked to veins for BE male
	{730, 728} // veins color linked to veins for BE female
};

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

	refreshGeosets();
	refreshTextures();

	for (const auto& c : choicesPerOptionMap_)
		set(c.first, c.second[0]);
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

	const auto infos = model_->infos;
	if (infos.raceID == -1)
		return;

	const auto options = WOWDB.getTable("ChrCustomizationOption");

	if (options)
	{
		// Collect matching options and sort by OrderIndex
		struct OptionEntry { uint id; uint orderIndex; };
		std::vector<OptionEntry> matchingOptions;
		for (const auto& row : *options)
		{
			if (row.getUInt("ChrModelID") == static_cast<uint32_t>(infos.ChrModelID[0]) &&
				row.getUInt("ChrCustomizationID") != 0)
			{
				matchingOptions.push_back({static_cast<uint>(row.recordID()), row.getUInt("OrderIndex")});
			}
		}
		std::sort(matchingOptions.begin(), matchingOptions.end(),
			[](const OptionEntry& a, const OptionEntry& b) { return a.orderIndex < b.orderIndex; });

		for (const auto& opt : matchingOptions)
			choicesPerOptionMap_[opt.id] = {};
	}

	LINKED_OPTIONS_MAP_.clear();
	initLinkedOptionsMap();

	for (const auto& option : choicesPerOptionMap_)
		fillCustomizationMapForOption(option.first);
}

void CharDetails::fillCustomizationMapForOption(uint chrCustomizationOption)
{
	auto& vals = choicesPerOptionMap_.at(chrCustomizationOption);
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
	const auto infos = model_->infos;
	if (infos.raceID == -1)
		return;

	currentCustomization_[chrCustomizationOptionID] = chrCustomizationChoiceID;
	customizationElementsPerOption_.erase(chrCustomizationOptionID);

	LOG_INFO << __FUNCTION__ << chrCustomizationOptionID << chrCustomizationChoiceID;
	const auto parentOptions = getParentOptions(chrCustomizationOptionID);
	const auto childOption = getChildOption(chrCustomizationOptionID);

	LOG_INFO << "Parent options for" << chrCustomizationOptionID;
	for (const auto& opt : parentOptions)
		LOG_INFO << "\t" << opt;
	LOG_INFO << "Child option for" << chrCustomizationOptionID;
	LOG_INFO << "\t" << childOption;

	auto choiceId = chrCustomizationChoiceID;
	auto relatedChoiceId = 0u;

	// 1. First query direct elements (related choice id = 0)
	if (!applyChrCustomizationElements(chrCustomizationOptionID, choiceId, relatedChoiceId))
	{
		LOG_ERROR << __FUNCTION__ << "No direct customization entry found for chrCustomizationOptionID" <<
			chrCustomizationOptionID << "/ chrCustomizationChoiceID" << chrCustomizationChoiceID;
	}

	// 2. Query elements coming from parent options
	for (const auto option : parentOptions)
	{
		if (option != -1)
		{
			relatedChoiceId = currentCustomization_[option];

			if (!applyChrCustomizationElements(option, choiceId, relatedChoiceId))
			{
				LOG_ERROR << __FUNCTION__ << "Parent Option" << option <<
					"-> No dependant customization entry found for chrCustomizationOptionID" << chrCustomizationOptionID
					<< "/ chrCustomizationChoiceID" << chrCustomizationChoiceID;
			}
		}
	}

	// 3. Query elements coming from child option
	if (childOption != -1)
	{
		// we are setting an option which have a dependant option, we need to set child choice with a new related choice (ie, we are setting tattoo, which needs to set tattoo color)
		choiceId = currentCustomization_[childOption];
		relatedChoiceId = chrCustomizationChoiceID;
		//customizationElementsPerOption_.erase(childOption);
		fillCustomizationMapForOption(childOption);

		if (!applyChrCustomizationElements(chrCustomizationOptionID, choiceId, relatedChoiceId))
		{
			LOG_ERROR << __FUNCTION__ << "Child option" << childOption <<
				"No dependant customization entry found for chrCustomizationOptionID" << chrCustomizationOptionID <<
				"/ chrCustomizationChoiceID" << chrCustomizationChoiceID;
		}
	}

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

	return choicesPerOptionMap_.at(chrCustomizationOptionID);
}

uint CharDetails::get(uint chrCustomizationOptionID) const
{
	return currentCustomization_.at(chrCustomizationOptionID);
}

bool CharDetails::applyChrCustomizationElements(uint chrCustomizationOption, uint choiceId, uint relatedChoiceId)
{
	const DB2Table* elementsTbl = WOWDB.getTable("ChrCustomizationElement");
	if (!elementsTbl)
		return false;

	// Collect matching elements
	struct ElementData { uint geosetID; uint skinnedModelID; uint materialID; uint boneSetID; uint condModelID; uint displayInfoID; uint id; };
	std::vector<ElementData> matchingElements;

	for (const auto& row : *elementsTbl)
	{
		if (row.getUInt("ChrCustomizationChoiceID") == choiceId &&
			row.getUInt("RelatedChrCustomizationChoiceID") == relatedChoiceId)
		{
			matchingElements.push_back({
				row.getUInt("ChrCustomizationGeosetID"),
				row.getUInt("ChrCustomizationSkinnedModelID"),
				row.getUInt("ChrCustomizationMaterialID"),
				row.getUInt("ChrCustomizationBoneSetID"),
				row.getUInt("ChrCustomizationCondModelID"),
				row.getUInt("ChrCustomizationDisplayInfoID"),
				static_cast<uint>(row.recordID())
			});
		}
	}

	LOG_INFO << __FUNCTION__ << chrCustomizationOption << matchingElements.size();

	if (!matchingElements.empty())
	{
		for (const auto& elt : matchingElements) // treat each element
		{
			if (elt.geosetID != 0) // geoset customization
			{
				LOG_INFO << "ChrCustomizationGeosetID based customization for" << elt.id << "/" << elt.geosetID;

				const DB2Table* geosetTbl = WOWDB.getTable("ChrCustomizationGeoset");
				if (geosetTbl)
				{
					DB2Row geoRow = geosetTbl->getRow(elt.geosetID);
					if (geoRow)
					{
						customizationElementsPerOption_[chrCustomizationOption].geosets.emplace_back(
							geoRow.getUInt("GeosetType"), geoRow.getUInt("GeosetID"));
					}
				}
			}
			else if (elt.skinnedModelID != 0) // added model customization
			{
				LOG_INFO << "ChrCustomizationSkinnedModelID based customization for" << elt.id << "/" << elt.skinnedModelID;
				const DB2Table* skinnedTbl = WOWDB.getTable("ChrCustomizationSkinnedModel");
				if (skinnedTbl)
				{
					DB2Row skinRow = skinnedTbl->getRow(elt.skinnedModelID);
					if (skinRow)
					{
						customizationElementsPerOption_[chrCustomizationOption].models.emplace_back(
							skinRow.getUInt("CollectionsFileDataID"),
							std::make_pair(skinRow.getUInt("GeosetType"), skinRow.getUInt("GeosetID")));
					}
				}
			}
			else if (elt.materialID != 0) // texture customization
			{
				LOG_INFO << "ChrCustomizationMaterialID based customization for" << elt.id << "/" << elt.materialID;

				// Resolve: ChrCustomizationMaterial -> MaterialResourcesID -> TextureFileData -> FileDataID
				//          ChrCustomizationMaterial -> ChrModelTextureTargetID -> ChrModelTextureLayer (with layout filter)
				const DB2Table* matTbl = WOWDB.getTable("ChrCustomizationMaterial");
				const DB2Table* texFileTbl = WOWDB.getTable("TextureFileData");
				const DB2Table* layerTbl = WOWDB.getTable("ChrModelTextureLayer");

				if (matTbl && texFileTbl && layerTbl)
				{
					DB2Row matRow = matTbl->getRow(elt.materialID);
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

						customizationElementsPerOption_[chrCustomizationOption].textures.push_back(t);
					}
				}
			}
			else if (elt.boneSetID != 0) // boneset customization ??
			{
				LOG_ERROR << "Not yet implemented ! boneset based customization for" << elt.id << "/" << elt.boneSetID;
			}
			else if (elt.condModelID != 0) // cond model customization ??
			{
				LOG_ERROR << "Not yet implemented ! Cond model based customization for" << elt.id << "/" << elt.condModelID;
			}
			else if (elt.displayInfoID != 0) // display info customization ??
			{
				LOG_ERROR << "Not yet implemented ! Display info based customization for" << elt.id << "/" << elt.displayInfoID;
			}
		}
		return true;
	}
	return false;
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

std::vector<int> CharDetails::getParentOptions(uint chrCustomizationOption)
{
	initLinkedOptionsMap();

	std::vector<int> result;

	const auto vals = LINKED_OPTIONS_MAP_.equal_range(chrCustomizationOption);

	for (auto it = vals.first; it != vals.second; ++it)
		result.push_back(it->second);

	return result;
}

int CharDetails::getChildOption(uint chrCustomizationOption)
{
	initLinkedOptionsMap();

	for (const auto& c : LINKED_OPTIONS_MAP_)
	{
		if (c.second == static_cast<int>(chrCustomizationOption))
			return static_cast<int>(c.first);
	}

	return -1;
}

void CharDetails::initLinkedOptionsMap()
{
	if (!LINKED_OPTIONS_MAP_.empty()) // already initialized
		return;

	const DB2Table* choicesTbl = WOWDB.getTable("ChrCustomizationChoice");
	const DB2Table* elementsTbl = WOWDB.getTable("ChrCustomizationElement");

	if (!choicesTbl || !elementsTbl)
		return;

	for (const auto& c : choicesPerOptionMap_)
	{
		auto id = c.first;

		// Inner: Find ChrCustomizationChoice ID where ChrCustomizationOptionID = id AND OrderIndex = 1
		uint32_t innerChoiceId = 0;
		for (const auto& row : *choicesTbl)
		{
			if (row.getUInt("ChrCustomizationOptionID") == id && row.getUInt("OrderIndex") == 1)
			{
				innerChoiceId = static_cast<uint32_t>(row.recordID());
				break;
			}
		}

		if (innerChoiceId == 0)
		{
			LINKED_OPTIONS_MAP_.emplace(id, -1);
			continue;
		}

		// Middle: Find ChrCustomizationElement rows where ChrCustomizationChoiceID = innerChoiceId
		//         -> collect RelatedChrCustomizationChoiceID
		std::set<uint32_t> relatedChoiceIds;
		for (const auto& row : *elementsTbl)
		{
			if (row.getUInt("ChrCustomizationChoiceID") == innerChoiceId)
			{
				const uint32_t relId = row.getUInt("RelatedChrCustomizationChoiceID");
				if (relId != 0)
					relatedChoiceIds.insert(relId);
			}
		}

		if (relatedChoiceIds.empty())
		{
			LINKED_OPTIONS_MAP_.emplace(id, -1);
			continue;
		}

		// Outer: Find ChrCustomizationChoice rows with ID in relatedChoiceIds -> collect DISTINCT ChrCustomizationOptionID
		std::set<int> linkedOptionIds;
		for (const auto& row : *choicesTbl)
		{
			if (relatedChoiceIds.count(static_cast<uint32_t>(row.recordID())) > 0)
				linkedOptionIds.insert(static_cast<int>(row.getUInt("ChrCustomizationOptionID")));
		}

		if (!linkedOptionIds.empty())
		{
			for (const auto& optId : linkedOptionIds)
				LINKED_OPTIONS_MAP_.emplace(id, optId);
		}
		else
		{
			LINKED_OPTIONS_MAP_.emplace(id, -1);
		}
	}
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

	if (showEars)
		geosets[CG_EARS] = 2;
	else
		geosets[CG_EARS] = 0;

	geosets[CG_FACE_1] = geosets[CG_FACE_2] = geosets[CG_FACE_3] = 0;

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

			// ond't display facila hairs if option is unchecked
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
