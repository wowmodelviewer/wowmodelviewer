#pragma once

#include <string>
#include <vector>

#define _CHARINFOS_API_

class _CHARINFOS_API_ CharInfos
{
public:
	CharInfos();

	bool valid;
	bool customTabard;

	unsigned int raceId;
	std::string gender;
	bool hasTransmogGear;

	unsigned int eyeGlowType;

	int tabardIcon;
	int iconColor;
	int tabardBorder;
	int borderColor;
	int background;

	std::vector<std::pair<unsigned int, unsigned int>> customizations; // vector<pair<optionId, choiceId>>

	// TODO refactor this part to associate slots, id and level
	std::vector<int> equipment;
	std::vector<int> itemModifierIds;
};
