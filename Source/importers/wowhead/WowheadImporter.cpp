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
 * WowheadImporter.cpp
 *
 *  Created on: 1 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#include "WowheadImporter.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "HttpClient.h"
#include "database.h" // ItemRecord
#include "NPCInfos.h"
#include "logger/Logger.h"

WowheadImporter::WowheadImporter() = default;

bool WowheadImporter::acceptURL(const std::string& url) const
{
	return (url.find("wowhead") != std::string::npos);
}

NPCInfos* WowheadImporter::importNPC(const std::string& urlToGrab) const
{
	// Get the HTML...
	std::string htmldata = getURLData(urlToGrab);
	if (htmldata.empty())
		return nullptr;

	// let's go : finding name
	// extract global infos
	std::string infos = extractSubString(htmldata, "(g_npcs[", ";");

	// finding name
	const std::string NPCName = extractSubString(infos, "name\":\"", "\",");

	// finding type
	const int NPCType = std::stoi(extractSubString(infos, "type\":", "}"));

	// finding id
	const int NPCId = std::stoi(extractSubString(infos, "id\":", ","));

	// display id
	std::string NPCDispIdstr = extractSubString(htmldata, "ModelViewer.show({");
	NPCDispIdstr = extractSubString(NPCDispIdstr, "displayId&quot;:", "}");

	auto commaPos = NPCDispIdstr.find(',');
	if (commaPos != std::string::npos) // comma at end of id
		NPCDispIdstr = NPCDispIdstr.substr(0, commaPos);

	const int NPCDispId = std::stoi(NPCDispIdstr);

	NPCInfos* result = new NPCInfos();

	result->name = std::wstring(NPCName.begin(), NPCName.end());
	result->type = NPCType;
	result->id = NPCId;
	result->displayId = NPCDispId;

	return result;
}

ItemRecord* WowheadImporter::importItem(const std::string& urlToGrab) const
{
	ItemRecord* result = nullptr;

	// Get the HTML...
	std::string htmldata = getURLData(urlToGrab);
	if (htmldata.empty())
		return nullptr;

	// let's go : finding name
	// extract global infos
	std::string data = extractSubString(htmldata, "(g_items[", ";");
	data = extractSubString(data, "],");
	if (!data.empty() && data.back() == ')')
		data.pop_back();

	const auto infos = nlohmann::json::parse(data, nullptr, false);

	if (infos.is_discarded())
	{
		LOG_INFO << "JSON parse error";
	}

	if (!infos.is_discarded() && infos.size() != 0)
	{
		result = new ItemRecord();

		result->name = infos["name"].get<std::string>();
		result->type = infos["slot"].get<int>();
		result->id = infos["id"].get<int>();
		result->model = infos["displayid"].get<int>();
		result->itemclass = infos["classs"].get<int>();
		result->subclass = infos["subclass"].get<int>();
	}

	return result;
}

std::string WowheadImporter::extractSubString(const std::string& datas, const std::string& beginPattern, const std::string& endPattern) const
{
	// Case-insensitive find helper
	auto findCI = [](const std::string& haystack, const std::string& needle, size_t pos) -> size_t {
		auto it = std::search(haystack.begin() + pos, haystack.end(),
			needle.begin(), needle.end(),
			[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
		return (it == haystack.end()) ? std::string::npos : static_cast<size_t>(it - haystack.begin());
	};

	size_t beginIdx = findCI(datas, beginPattern, 0);
	if (beginIdx == std::string::npos)
		return {};

	beginIdx += beginPattern.size();
	std::string result = datas.substr(beginIdx);

	if (!endPattern.empty())
	{
		size_t endIdx = findCI(result, endPattern, 0);
		if (endIdx != std::string::npos)
			result = result.substr(0, endIdx);
	}
	return result;
}

std::string WowheadImporter::getURLData(const std::string& inputUrl) const
{
	const auto resp = HttpClient::Get(inputUrl);
	if (!resp.success)
	{
		LOG_ERROR << "HTTP request failed: " << resp.error;
		return {};
	}
	return resp.body;
}
