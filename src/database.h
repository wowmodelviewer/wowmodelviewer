#ifndef DATABASE_H
#define DATABASE_H

// Combined the previous 5 various "db" files into one.
// trying to cut down on excess files.
// Also instead of declaring the db objects inside various classes
// may aswell declare them as globals since pretty much most the
// different objects need to access them at one point or another.

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// WX
#include <wx/string.h>

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
#include "enums.h"
#include "util.h"

// wmv database
class ItemDatabase;
class NPCRecord;

extern ItemDatabase items;
extern std::vector<NPCRecord> npcs;

// game database
class HelmGeosetDB;
class ItemVisualEffectDB;
class ItemSetDB;
class StartOutfitDB;
class ItemVisualDB;
class LightSkyBoxDB;
class SpellItemEnchantmentDB;
class ItemVisualsDB;
class CamCinematicDB;

extern HelmGeosetDB	helmetdb;
extern ItemVisualEffectDB effectdb;
extern ItemSetDB setsdb;
extern StartOutfitDB startdb;
extern ItemVisualDB visualdb;
extern LightSkyBoxDB skyboxdb;
extern SpellItemEnchantmentDB spellitemenchantmentdb;
extern ItemVisualsDB itemvisualsdb;
extern CamCinematicDB camcinemadb;

class SpellItemEnchantmentDB: public DBCFile
{
public:
	SpellItemEnchantmentDB(): DBCFile(wxT("DBFilesClient\\SpellItemEnchantment.dbc")) {}
	~SpellItemEnchantmentDB() {}

	// Fields
	static const size_t Name = 14;		// string, localization
	static const size_t VisualID = 31;	// unit

	static const size_t VisualIDV400 = 15;
};

class ItemVisualsDB: public DBCFile
{
public:
	ItemVisualsDB(): DBCFile(wxT("DBFilesClient\\ItemVisuals.dbc")) {}
	~ItemVisualsDB() {}

	// Fields
	static const size_t VisualID = 0;	// unit
};

class LightSkyBoxDB: public DBCFile
{
public:
	LightSkyBoxDB(): DBCFile(wxT("DBFilesClient\\LightSkybox.dbc")) {}
	~LightSkyBoxDB() {}

	// Fields
	// static const size_t ID;
	static const size_t Name = 1;		// string
	// static const size_t Flags;
};

// ============
class HelmGeosetDB: public DBCFile
{
public:
	HelmGeosetDB(): DBCFile(wxT("DBFilesClient\\HelmetGeosetVisData.dbc")) {}
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

class ItemVisualDB: public DBCFile
{
public:
	ItemVisualDB(): DBCFile(wxT("DBFilesClient\\ItemVisuals.dbc")) {}
	~ItemVisualDB() {}

	/// Fields
	static const size_t VisualID = 0;	// uint
	static const size_t Effect1 = 1;	// uint
	//static const size_t Effect2 = 2;	// uint
	//static const size_t Effect3 = 3;	// uint
	//static const size_t Effect4 = 4;	// uint
	//static const size_t Effect5 = 5;	// uint

	Record getById(unsigned int id);
};

class ItemVisualEffectDB: public DBCFile
{
public:
	ItemVisualEffectDB(): DBCFile(wxT("DBFilesClient\\ItemVisualEffects.dbc")) {}
	~ItemVisualEffectDB() {}

	/// Fields
	static const size_t EffectID = 0;	// uint
	static const size_t Model = 1;		// string

	Record getById(unsigned int id);
};


class ItemSetDB: public DBCFile
{
	std::set<int> avail;

public:
	ItemSetDB(): DBCFile(wxT("DBFilesClient\\ItemSet.dbc")) {}
	~ItemSetDB() {}

	static const size_t NumItems = 10;

	/// Fields
	static const size_t SetID = 0;	// uint
	static const size_t Name = 1;	// string, Localization
	static const size_t ItemIDBase = 18; // 10 * uint

	Record getById(unsigned int id);
	void cleanup(ItemDatabase &l_itemdb);
	bool available(unsigned int id);
	
	static const size_t ItemIDBaseV400 = 2; // 10 * uint
};

class StartOutfitDB: public DBCFile
{
public:
	StartOutfitDB(): DBCFile(wxT("DBFilesClient\\CharStartOutfit.dbc")) {}
	~StartOutfitDB() {}

	static const size_t NumItems = 24;

	/// Fields
	static const size_t StartOutfitID = 0;	// uint
	static const size_t Race = 4;	// byte offset
	static const size_t Class = 5;	// byte offset
	static const size_t Gender = 6;	// byte offset
	static const size_t ItemIDBase = 2; // 24 * uint
//	static const size_t ItemDisplayIDBase = 26; // 24 * uint
//	static const size_t ItemTypeBase = 50; // 24 * uint

	Record getById(unsigned int id);
};

struct ItemRecord {
	wxString name;
	int id, itemclass, subclass, type, model, sheath, quality;

	ItemRecord(const std::vector<std::string> &);
	ItemRecord():id(0), itemclass(-1), subclass(-1), type(0), model(0), sheath(0), quality(0)
	{}
};

class ItemDatabase {
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
struct NPCRecord 
{
	wxString name;
	int id, model, type;

	NPCRecord(wxString line);
	NPCRecord(const std::vector<std::string> &);
	NPCRecord(): id(0), model(0), type(0) {}
	NPCRecord(const NPCRecord &r): name(r.name), id(r.id), model(r.model), type(r.type) {}

};

// =========================================

class SpellEffectsDB: public DBCFile
{
public:
	SpellEffectsDB(): DBCFile(wxT("DBFilesClient\\SpellVisualEffectName.db2")) {}
	~SpellEffectsDB() {}

	/// Fields
	static const size_t ID = 0;				// uint
	static const size_t EffectName = 1;		// string
	//static const size_t ModelName = 2;		// string
	//static const size_t SpellType = 3;		// uint
	//static const size_t UnknownValue2 = 4;	// uint

	Record getById(unsigned int id);
	Record getByName(wxString name);
};


// ===============================================

class CamCinematicDB: public DBCFile
{
public:
	CamCinematicDB(): DBCFile(wxT("DBFilesClient\\CinematicCamera.dbc")) {}
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
	Record getByCamModel(wxString fn);
};


#endif

