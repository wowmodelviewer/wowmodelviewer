#pragma once

// Combined the previous 5 various "db" files into one.
// trying to cut down on excess files.
// Also instead of declaring the db objects inside various classes
// may aswell declare them as globals since pretty much most the
// different objects need to access them at one point or another.

// STL
#include <vector>
#include <map>

#include <string>

// wmv database
class ItemDatabase;
struct NPCRecord;

class ItemDatabase;

extern ItemDatabase items;
extern std::vector<NPCRecord> npcs;

struct ItemRecord
{
	std::string name;
	int id, itemclass, subclass, type, model, sheath, quality;

	ItemRecord(const std::vector<std::string>&);

	ItemRecord(): id(0), itemclass(-1), subclass(-1), type(0), model(0), sheath(0), quality(0)
	{
	}

	int slot();
};

class ItemDatabase
{
public:
	ItemDatabase();

	std::vector<ItemRecord> items;
	std::map<int, int> itemLookup;

	const ItemRecord& getById(int id);
};

struct NPCRecord
{
	std::string name;
	int id, model, type;

	NPCRecord(const std::string& line);
	NPCRecord(const std::vector<std::string>&);

	NPCRecord(): id(0), model(0), type(0)
	{
	}
};
