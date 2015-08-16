#ifndef DATABASE_H
#define DATABASE_H

// Combined the previous 5 various "db" files into one.
// trying to cut down on excess files.
// Also instead of declaring the db objects inside various classes
// may aswell declare them as globals since pretty much most the
// different objects need to access them at one point or another.

// STL
#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <set>


// OUR HEADERS
#include "dbcfile.h"

// wmv database
class ItemDatabase;
struct NPCRecord;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _DATABASE_API_ __declspec(dllexport)
#    else
#        define _DATABASE_API_ __declspec(dllimport)
#    endif
#else
#    define _DATABASE_API_
#endif


_DATABASE_API_ extern ItemDatabase items;
_DATABASE_API_ extern std::vector<NPCRecord> npcs;

// game database
class HelmGeosetDB;
class LightSkyBoxDB;
class CamCinematicDB;

_DATABASE_API_ extern HelmGeosetDB	helmetdb;
_DATABASE_API_ extern LightSkyBoxDB skyboxdb;
_DATABASE_API_ extern CamCinematicDB camcinemadb;


class _DATABASE_API_ LightSkyBoxDB: public DBCFile
{
public:
	LightSkyBoxDB(): DBCFile("DBFilesClient\\LightSkybox.dbc") {}
	~LightSkyBoxDB() {}

	// Fields
	// static const size_t ID;
	static const size_t Name = 1;		// string
	// static const size_t Flags;
};

// ============
class _DATABASE_API_ HelmGeosetDB: public DBCFile
{
public:
	HelmGeosetDB(): DBCFile("DBFilesClient\\HelmetGeosetVisData.dbc") {}
	~HelmGeosetDB() {}

	/// Fields
	static const size_t TypeID = 0;		// uint
	static const size_t Hair = 1;		// int Hair, 0 = show, anything else = don't show? eg: a value of 1020 won't hide night elf ears, but 999999 or -1 will.
	static const size_t Facial1Flags = 2;		// int Beard or Tusks
	static const size_t Facial2Flags = 3;		// int Earring
	static const size_t Facial3Flags = 4;		// int, See ChrRaces, column 24 to 26 for information on what is what.
	static const size_t EarsFlags = 5;		// int Ears
	//static const size_t Field6 = 6;		// int
	//static const size_t Field7 = 7;		// int

	Record getById(unsigned int id);
};

// ==============================================

// -----------------------------------
// Item Stuff
// -----------------------------------

class ItemDatabase;


struct _DATABASE_API_ ItemRecord {
	QString name;
	int id, itemclass, subclass, type, model, sheath, quality;

	ItemRecord(const std::vector<QString> &);
	ItemRecord():id(0), itemclass(-1), subclass(-1), type(0), model(0), sheath(0), quality(0)
	{}

	int slot();
};

class _DATABASE_API_ ItemDatabase {
public:
	ItemDatabase();

	std::vector<ItemRecord> items;
	std::map<int, int> itemLookup;

	const ItemRecord& getById(int id);
};

// ============/////////////////=================/////////////////


// ------------------------------
// NPC Stuff
// -------------------------------
struct _DATABASE_API_ NPCRecord
{
	QString name;
	int id, model, type;

	NPCRecord(QString line);
	NPCRecord(const std::vector<QString> &);
	NPCRecord(): id(0), model(0), type(0) {}
	NPCRecord(const NPCRecord &r): name(r.name), id(r.id), model(r.model), type(r.type) {}

};

// ===============================================

class _DATABASE_API_ CamCinematicDB: public DBCFile
{
public:
	CamCinematicDB(): DBCFile("DBFilesClient\\CinematicCamera.dbc") {}
	~CamCinematicDB() {}

	// WotLK Fields
	static const size_t CamCineID = 0;		// uint
	static const size_t CamModel = 1;		// string, ends in .mdx
	static const size_t VoiceoverID = 2;	// uint
	static const size_t PosX = 3;			// float
	static const size_t PosZ = 4;			// float
	static const size_t PosY = 5;			// float
	static const size_t Rot = 6;			// float

	Record getById(unsigned int id);
	Record getByCamModel(std::string fn);
};


#endif

