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

#define _DATABASE_API_

class ItemDatabase;

_DATABASE_API_ extern ItemDatabase items;
_DATABASE_API_ extern std::vector<NPCRecord> npcs;

struct _DATABASE_API_ ItemRecord
{
	std::string name;
	int id, itemclass, subclass, type, model, sheath, quality;

	ItemRecord(const std::vector<std::string>&);

	ItemRecord(): id(0), itemclass(-1), subclass(-1), type(0), model(0), sheath(0), quality(0)
	{
	}

	int slot();
};

class _DATABASE_API_ ItemDatabase
{
public:
	ItemDatabase();

	std::vector<ItemRecord> items;
	std::map<int, int> itemLookup;

	const ItemRecord& getById(int id);
};

struct _DATABASE_API_ NPCRecord
{
	std::string name;
	int id, model, type;

	NPCRecord(const std::string& line);
	NPCRecord(const std::vector<std::string>&);

	NPCRecord(): id(0), model(0), type(0)
	{
	}

	NPCRecord(const NPCRecord& r): name(r.name), id(r.id), model(r.model), type(r.type)
	{
	}

	NPCRecord& operator=(const NPCRecord& r)
	{
		if (this != &r)
		{
			name = r.name;
			id = r.id;
			model = r.model;
			type = r.type;
		}
		return *this;
	}
};
