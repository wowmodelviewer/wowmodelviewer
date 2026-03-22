#pragma once

#include <string>
#include <vector>

/// @brief Stores imported character information (race, gender, equipment, customisations, tabard).
class CharInfos
{
public:
	CharInfos();

	bool valid;           ///< Whether the data was successfully parsed.
	bool customTabard;    ///< Whether a custom tabard design is applied.

	unsigned int raceId;  ///< Character race ID.
	std::string gender;   ///< Gender string ("male" or "female").
	bool hasTransmogGear; ///< Whether transmog overrides are present.

	unsigned int eyeGlowType;  ///< Eye glow type (none, default, death knight).

	int tabardIcon;    ///< Tabard icon index.
	int iconColor;     ///< Tabard icon colour index.
	int tabardBorder;  ///< Tabard border index.
	int borderColor;   ///< Tabard border colour index.
	int background;    ///< Tabard background index.

	std::vector<std::pair<unsigned int, unsigned int>> customizations; ///< Customisation pairs (optionId, choiceId).

	std::vector<int> equipment;       ///< Equipment item IDs per slot.
	std::vector<int> itemModifierIds;  ///< Item modifier IDs per slot.
};
