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
 * WowheadImporter.h
 *
 *  Created on: 1 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#pragma once

#include <string>

#include "ImporterPlugin.h"

/// @brief Imports NPC and item data by scraping Wowhead web pages.
class WowheadImporter : public ImporterPlugin //-V1106
{

public:
	WowheadImporter();

	~WowheadImporter() = default;

	bool acceptURL(const std::string& url) const;

	NPCInfos* importNPC(const std::string& url) const;
	CharInfos* importChar(const std::string& url) const { return nullptr; }
	ItemRecord* importItem(const std::string& url) const;

private:
	std::string extractSubString(const std::string& datas, const std::string& beginPattern, const std::string& endPattern = {}) const;
	std::string getURLData(const std::string& inputUrl) const;
};
