/*
* CharDetails.h
*
*  Created on: 26 oct. 2013
*
*/

#pragma once

#include <set>
#include <string>

#include "CharTexture.h"
#include "database.h"
#include "RaceInfos.h"
#include "types.h"
#include "wow_enums.h"

#include "Observable.h"

#include "pugixml.hpp"

class WoWModel;

/// @brief Manages character customisation options (skin, face, hair, etc.).
///
/// Maintains the current selection indices for each customisation slot and
/// notifies observers when selections change so textures can be rebuilt.
class CharDetails : public Observable
{
public:
	CharDetails();

	// Types
	enum BaseSectionType
	{
		SkinBaseType = 0,
		FaceBaseType = 1,
		FacialHairBaseType = 2,
		HairBaseType = 3,
		UnderwearBaseType = 4,
		Custom1BaseType = 5,
		Custom2BaseType = 6,
		Custom3BaseType = 7
	};

	// Types
	enum SectionType
	{
		SkinType = 0,
		FaceType = 1,
		FacialHairType = 2,
		HairType = 3,
		UnderwearType = 4,
		SkinTypeHD = 5,
		FaceTypeHD = 6,
		FacialHairTypeHD = 7,
		HairTypeHD = 8,
		UnderwearTypeHD = 9,
		Custom1Type = 11,
		Custom1TypeHD = 12,
		Custom2Type = 13,
		Custom2TypeHD = 14,
		Custom3Type = 15,
		Custom3TypeHD = 16
	};

	enum CustomizationType
	{
		SKIN_COLOR = 0,
		FACE = 1,
		FACIAL_CUSTOMIZATION_STYLE = 2,
		FACIAL_CUSTOMIZATION_COLOR = 3,
		ADDITIONAL_FACIAL_CUSTOMIZATION = 4,
		CUSTOM1_STYLE = 5,
		CUSTOM1_COLOR = 8,
		CUSTOM2_STYLE = 6,
		CUSTOM3_STYLE = 7
	};

	class TextureCustomization
	{
	public:
		uint layer;
		int region;
		uint type;
		uint fileId;
		uint blendMode;
	};

	EyeGlowTypes eyeGlowType;

	bool showUnderwear, showEars, showHair, showFacialHair, showFeet, autoHideGeosetsForHeadItems;

	bool isNPC;

	std::map<uint, uint> geosets; // map <geoset type, geosetid>
	std::vector<TextureCustomization> textures;

	// save + load
	void save(pugi::xml_node& parentNode);
	void load(const std::string& filepath);

	void reset(WoWModel* m = nullptr);
	void randomise();

	// accessors to customization
	uint get(uint chrCustomizationOptionID) const;
	void set(uint chrCustomizationOptionID, uint chrCustomizationChoiceID);
	std::vector<uint> getCustomizationChoices(const uint chrCustomizationOptionID);

	void setDemonHunterMode(bool val) { isDemonHunter_ = val; }
	bool isDemonHunter() const { return isDemonHunter_; }

	void refresh();

private:
	void fillCustomizationMap();

	WoWModel* model_;
	bool isDemonHunter_;

	std::map<uint, uint> currentCustomization_; // map <ChrCustomizationOption::ID, ChrCustomizationChoice::ID>

	class CustomizationElements
	{
	public:
		std::vector<std::pair<uint, uint>> geosets; // std::vector<std::pair<GeosetType, GeosetID>>
		std::vector<TextureCustomization> textures;
		std::vector<std::pair<uint, std::pair<uint, uint>>> models;
		// std::vector<std::pair<GameFileId, std::pair<GeosetType, GeosetID>>>
		void clear()
		{
			geosets.clear();
			textures.clear();
			models.clear();
		}
	};

	void fillCustomizationMapForOption(uint chrCustomizationOption);
	void rebuildAllCustomizationElements();
	static int bitMaskToSectionType(int mask);
	void refreshGeosets();
	void refreshTextures();
	void refreshSkinnedModels();

	std::map<uint, std::vector<uint>> choicesPerOptionMap_;
	// map < ChrCustomizationOption::ID, vector <ChrCustomizationChoice::ID> >
	std::set<uint> defaultOptionIds_;
	// options without Flags & 0x20 get a default choice on reset (matches wow.export)
	std::map<uint, CustomizationElements> customizationElementsPerOption_;
	// keep track of current elements applied for a given option
	std::vector<std::pair<uint, std::pair<uint, uint>>> models_;
	// vector < pair < GameFileId, pair <GeosetType, GeosetID> > >
};
