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
 * ArmoryImporter.h
 *
 *  Created on: 9 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "ImporterPlugin.h"

/// @brief Imports character and item data from the World of Warcraft Armory API.
class ArmoryImporter final : public ImporterPlugin //-V1106
{

public:
	ArmoryImporter();
	~ArmoryImporter() = default;

	bool acceptURL(const std::string& url) const override;

	NPCInfos* importNPC(const std::string& url) const override { return nullptr; };
	CharInfos* importChar(const std::string& url) const override;
	ItemRecord* importItem(const std::string& url) const override;

private:
	enum ImportType
	{
		CHARACTER,
		ITEM
	};

	int readJSONValues(ImportType type, const std::string& url, nlohmann::json& result) const;
	std::string getURLData(const std::string& inputUrl) const;
	static bool hasMember(const nlohmann::json& check, const std::string& lookfor);
	static bool hasTransmog(const nlohmann::json& check);
};
