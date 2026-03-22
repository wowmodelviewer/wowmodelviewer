#pragma once

#include <string>

struct ItemRecord;
class CharInfos;
class NPCInfos;

/// @brief Abstract base class for URL-based import plugins (Armory, Wowhead, etc.).
///
/// Subclasses detect whether they can handle a given URL and import
/// character, NPC, or item data from it.
class ImporterPlugin
{
public:
	ImporterPlugin() = default;
	virtual ~ImporterPlugin() = default;

	virtual bool acceptURL(const std::string& url) const = 0;
	virtual NPCInfos* importNPC(const std::string& url) const = 0;
	virtual ItemRecord* importItem(const std::string& url) const = 0;
	virtual CharInfos* importChar(const std::string& url) const = 0;
};
