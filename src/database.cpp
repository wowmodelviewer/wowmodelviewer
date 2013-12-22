#include "database.h"

#include "globalvars.h"
#include "modelviewer.h"
#include "mpq.h"


ItemDatabase		items;
// dbs
NPCDatabase			npcs;

// --
HelmGeosetDB		helmetdb;
ItemVisualEffectDB	effectdb;
ItemDisplayDB		itemdisplaydb;
StartOutfitDB		startdb;
ItemSubClassDB		subclassdb;
ItemVisualDB		visualdb;
ItemSetDB			setsdb;
ItemDB				itemdb;
ItemSparseDB		itemsparsedb;
// --
AnimDB				animdb;
CharHairGeosetsDB	hairdb;
CharSectionsDB		chardb;
CharClassesDB		classdb;
CharFacialHairDB	facialhairdb;
CharRacesDB			racedb;
//--
CreatureModelDB		modeldb;
CreatureSkinDB		skindb;
CreatureTypeDB		npctypedb;
NPCDB				npcdb;
LightSkyBoxDB			skyboxdb;
SpellItemEnchantmentDB	spellitemenchantmentdb;
ItemVisualsDB			itemvisualsdb;
CamCinematicDB		 camcinemadb;

// ANIMDB.H
AnimDB::Record AnimDB::getByAnimID(unsigned int id)
{
	/// Brute force search for now
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if(i->getUInt(AnimID) == id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}
// --
// CAMCINEMADB.H
CamCinematicDB::Record CamCinematicDB::getByCamModel(wxString fn)
{
	wxLogMessage(wxT("Searching for CamModel..."));
	// Brute force search for now
	for (Iterator i=begin(); i!=end(); ++i)
	{
		//wxLogMessage(wxT("Iteration %i"),i);
		wxString str(i->getString(CamModel));
		wxLogMessage(wxT("CamModel: %s, VS %s"), str.c_str(), fn.c_str());
		if(str.IsSameAs(fn, false) == true)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

// --
// CHARDB.H
// HairGeosets

CharHairGeosetsDB::Record CharHairGeosetsDB::getByParams(unsigned int race, unsigned int gender, unsigned int section)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Section)==section)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

int CharHairGeosetsDB::getGeosetsFor(unsigned int race, unsigned int gender)
{
	int n = 0;
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(Race)==race && i->getUInt(Gender)==gender) {
			n++;
		}
	}
    return n;
}

// Sections

int CharSectionsDB::getColorsFor(size_t race, size_t gender, size_t type, size_t section, size_t npc)
{
	int n = 0;
	for(Iterator i=begin(); i!=end(); ++i)
	{
		// don't allow NPC skins ;(
		if (gameVersion < VERSION_WOTLK) {
			if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(SectionBC)==section && i->getUInt(IsNPCBC)==npc) {
				n++;
			}
		} else {
			if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(Section)==section) {
				n++;
			}
		}
	}

    return n;
}

int CharSectionsDB::getSectionsFor(size_t race, size_t gender, size_t type, size_t color, size_t npc)
{
	int n = 0;
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (gameVersion < VERSION_WOTLK) {
			if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(ColorBC)==color && i->getUInt(IsNPCBC)==npc) {
				n++;
			}
		} else {
			if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(Color)==color) {
				n++;
			}
		}
	}
    return n;
}

CharSectionsDB::Record CharSectionsDB::getByParams(size_t race, size_t gender, size_t type, size_t section, size_t color, size_t npc)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (gameVersion < VERSION_WOTLK) {
			if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(SectionBC)==section && i->getUInt(ColorBC)==color && i->getUInt(IsNPCBC)==npc)
				return (*i);
		} else {
			if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(Section)==section && i->getUInt(Color)==color)
				return (*i);
		}
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d race:%d, gender:%d, type:%d, section:%d, color:%d"), __FILE__, __FUNCTION__, __LINE__, race, gender, type, section, color);
	throw NotFound();
}

// Races
CharRacesDB::Record CharRacesDB::getByName(wxString name)
{
	for(Iterator i=begin(); i!=end(); ++i) {
		wxString r;
		if (gameVersion == 30100)
			r = i->getString(NameV310);
		else
			r = i->getString(Name);
		if (name.IsSameAs(r, false) == true)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

CharRacesDB::Record CharRacesDB::getById(size_t id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(RaceID)==id) return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}


// FacialHair

CharFacialHairDB::Record CharFacialHairDB::getByParams(unsigned int race, unsigned int gender, unsigned int style)
{
	if (gameVersion >= VERSION_CATACLYSM) {
		for(Iterator i=begin(); i!=end(); ++i)
		{
			if (i->getUInt(RaceV400)==race && i->getUInt(GenderV400)==gender && i->getUInt(StyleV400)==style)
				return (*i);
		}
	} else {
		for(Iterator i=begin(); i!=end(); ++i)
		{
			if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Style)==style)
				return (*i);
		}
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

int CharFacialHairDB::getStylesFor(unsigned int race, unsigned int gender)
{
	int n = 0;
	if (gameVersion >= VERSION_CATACLYSM) {
		for(Iterator i=begin(); i!=end(); ++i)
		{
			if (i->getUInt(RaceV400)==race && i->getUInt(GenderV400)==gender) {
				n++;
			}
		}
	} else {
		for(Iterator i=begin(); i!=end(); ++i)
		{
			if (i->getUInt(Race)==race && i->getUInt(Gender)==gender) {
				n++;
			}
		}
	}
	return n;
}


// Classes
CharClassesDB::Record CharClassesDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(ClassID)==id) return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}


// Head and Helmet display info
HelmGeosetDB::Record HelmGeosetDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(TypeID)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}
// --


// --
// CREATUREDB.H
//
CreatureModelDB::Record CreatureModelDB::getByFilename(wxString fn)
{
	int nbtests = 0;
	/// Brute force search for now
	for(Iterator i=begin(); i!=end(); ++i)
	{
		nbtests++;
		wxString str(i->getString(CreatureModelDB::Filename));

		str = str.BeforeLast(wxT('.'));

		if(str.IsSameAs(fn, false) == true)
			return (*i);
	}
	wxLogMessage(wxT("CreatureModelDB::getByFilename %s => %i tests !!! NOT FOUND !!!"), fn.c_str(), nbtests);
	throw NotFound();
}

CreatureModelDB::Record CreatureModelDB::getByID(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(ModelID)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

CreatureSkinDB::Record CreatureSkinDB::getByModelID(unsigned int id)
{
	/// Brute force search for now
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if(i->getUInt(ModelID) == id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

CreatureSkinDB::Record CreatureSkinDB::getBySkinID(unsigned int id)
{
	/// Brute force search for now
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if(i->getUInt(SkinID) == id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

CreatureTypeDB::Record CreatureTypeDB::getByID(unsigned int id)
{
	/// Brute force search for now
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if(i->getUInt(ID) == id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

NPCDB::Record NPCDB::getByFilename(wxString fn)
{
	/// Brute force search for now
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if(i->getString(Filename) == fn) {
			//std::cout << i->getString(Filename).c_str() << "\n";
			return (*i);
		}
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

NPCDB::Record NPCDB::getByNPCID(size_t id)
{
	/// Brute force search for now
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if(i->getUInt(NPCID) == (unsigned int)id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

// --



// --
// ITEMDB.H
//
// --------------------------------
// Item Database Stuff
// --------------------------------
/*
const char* ItemTypeNames[NUM_ITEM_TYPES] = {
	"All",
	"Helmets",
	"Neck",
	"Shoulder armor",
	"Shirts",
	"Chest armor",
	"Belts",
	"Pants",
	"Boots",
	"Bracers",
	"Gloves",
	"Rings",
	"Accessories",
	"Daggers",
	"Shields",
	"Bows",
	"Capes",
	"Two-handed weapons",
	"Quivers",
	"Tabards",
	"Robes",
	"One-handed weapons",
	"Offhand weapons",
	"Holdable",
	"Ammo",
	"Thrown",
	"Guns and wands",
	"Unknown",
	"Relic"
};
*/

// ItemDisplayInfo

ItemDisplayDB::Record ItemDisplayDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(ItemDisplayID)==id)
			return (*i);
	}
	wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}


ItemVisualDB::Record ItemVisualDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(VisualID)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

ItemVisualEffectDB::Record ItemVisualEffectDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(EffectID)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

ItemSetDB::Record ItemSetDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(SetID)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

void ItemSetDB::cleanup(ItemDatabase &p_itemdb)
{
	for(Iterator i=begin(); i!=end(); ++i) {
		for (size_t j=0; j<NumItems; j++) {
			int id;
			if (gameVersion >= VERSION_CATACLYSM)
				id = i->getUInt(ItemIDBaseV400+j);
			else
				id = i->getUInt(ItemIDBase+j);
			if (id > 0) {
				const ItemRecord &r = p_itemdb.getById(id);
				if (r.type > 0) {
					avail.insert(i->getUInt(SetID));
					break;
				}
			}
		}
	}
}

bool ItemSetDB::available(unsigned int id)
{
	return (avail.find(id)!=avail.end());
}


StartOutfitDB::Record StartOutfitDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(StartOutfitID)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}



////////////////////

ItemDB::Record ItemDB::getById(unsigned int id)
{
	if (!inited) {
		int j=0;
		for(Iterator i=begin(); i!=end(); ++i)
		{
			itemLookup[i->getUInt(ID)] = j;
			itemDisplayLookup[i->getUInt(ItemDisplayInfo)] = j;
			j++;
		}
		inited = true;
	}

    if (itemLookup.find(id)!=itemLookup.end()) {
		int i = itemLookup[id];
		ItemDB::Record rec = itemdb.getRecord(i);
		return rec;
    }
	throw NotFound();

/*
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(ID)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d id:%d"), __FILE__, __FUNCTION__, __LINE__, id);
	throw NotFound();
*/
}

ItemDB::Record ItemDB::getByDisplayId(unsigned int id)
{
	if (!inited) {
		int j=0;
		for(Iterator i=begin(); i!=end(); ++i)
		{
			itemLookup[i->getUInt(ID)] = j;
			itemDisplayLookup[i->getUInt(ItemDisplayInfo)] = j;
			j++;
		}
		inited = true;
	}

    if (itemDisplayLookup.find(id)!=itemDisplayLookup.end()) {
		int i = itemDisplayLookup[id];
		ItemDB::Record rec = itemdb.getRecord(i);
		return rec;
    }
	throw NotFound();

/*
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(ItemDisplayInfo)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d id:%d"), __FILE__, __FUNCTION__, __LINE__, id);
*/
	throw NotFound();
}

void ItemSparseDB::init()
{
	int id, qualityid;
	wxString name, itemstring;
	//wxLogMessage(wxT("gameVersion: %d"), gameVersion);

	// Item-sparse.db2 didn't appear until Cataclysm
	if (gameVersion < VERSION_CATACLYSM)
		return;

	for(Iterator i=begin(); i!=end(); ++i) {
		id = i->getInt(ID);
		qualityid = i->getInt(QualityID);
		if (gameVersion <= 40200)
			name = i->getString(Name40200);
		else if (gameVersion >= 40300)
			name = i->getString(Name40300);
		//wxLogMessage(wxT("ItemSparseDB::init %d,%d,%s"), id, qualityid, name.c_str());
		itemstring = wxString::Format(wxT("%d,%d,"), id, qualityid) + name;
		ItemRecord rec(itemstring);
		if (rec.type > 0) {
			items.items.push_back(rec);
		}
	}
}

// old format, deprecated
void ItemRecord::getLine(const char *line)
{
	sscanf(line, "%u,%u,%u,%u,%u,%u,%u", &id, &model, &itemclass, &subclass, &type, &sheath, &quality); // failed with MacOS
	for (size_t i=strlen(line)-2; i>1; i--) {
		if (line[i]==',') {
			name = wxString(line + i + 1, wxConvUTF8);
			break;
		}
	}
	discovery = true;
}

ItemRecord::ItemRecord(wxString line)
    : type(0)
{
	id = wxAtoi(line.BeforeFirst(','));
	line = line.AfterFirst(',');
	quality = wxAtoi(line.BeforeFirst(','));
	line = line.AfterFirst(',');
	try {
		ItemDB::Record r = itemdb.getById(id);
		model = r.getInt(ItemDB::ItemDisplayInfo);
		itemclass = r.getInt(ItemDB::Itemclass);
		subclass = r.getInt(ItemDB::Subclass);
		type = r.getInt(ItemDB::InventorySlot);
		switch(r.getInt(ItemDB::Sheath)) {
			case SHEATHETYPE_MAINHAND: sheath = ATT_LEFT_BACK_SHEATH; break;
			case SHEATHETYPE_LARGEWEAPON: sheath = ATT_LEFT_BACK; break;
			case SHEATHETYPE_HIPWEAPON: sheath = ATT_LEFT_HIP_SHEATH; break;
			case SHEATHETYPE_SHIELD: sheath = ATT_MIDDLE_BACK_SHEATH; break;
			default: sheath = SHEATHETYPE_NONE;
		}
		discovery = false;
		name.Printf(wxT("%s [%d] [%d]"), line.c_str(), id, model);
	} catch (ItemDB::NotFound) {}

}

// Alfred. prevent null items bug.
ItemDatabase::ItemDatabase()
{
	ItemRecord all(wxT("---- None ----"), IT_ALL);
	items.push_back(all);
}

void ItemDatabase::open(wxString filename)
{
	// 1. in-game db
	itemsparsedb.init();

	// 2. items.csv
	wxTextFile fin;
	if (wxFileExists(filename) && fin.Open(filename)) {
		wxString line;
		for ( line = fin.GetFirstLine(); !fin.Eof(); line = fin.GetNextLine() ) {
			ItemRecord rec(line);
			if (rec.type > 0) {
				items.push_back(rec);
			}
		}
		fin.Close();
	}

	// 3. discoveryitems.csv
	wxTextFile fin2;;
	if (wxFileExists(wxT("discoveryitems.csv")) && fin2.Open(wxT("discoveryitems.csv"))) {
		wxString line;
		g_modelViewer->SetStatusText(wxT("Initialing discoveryitems.csv Database..."));
		for ( line = fin2.GetFirstLine(); !fin2.Eof(); line = fin2.GetNextLine() ) {
			ItemRecord rec;
			rec.getLine((char *)line.c_str());
			if (rec.type > 0) {
				items.push_back(rec);
			}
		}
		fin2.Close();
		g_modelViewer->fileMenu->Enable(ID_FILE_DISCOVERY_ITEM, false);
	}

	sort(items.begin(), items.end());
}

void ItemDatabase::cleanup(ItemDisplayDB &l_itemdisplaydb)
{
	std::set<unsigned int> itemset;
	for (ItemDisplayDB::Iterator it = l_itemdisplaydb.begin(); it != l_itemdisplaydb.end(); ++it) {
		itemset.insert(it->getUInt(ItemDisplayDB::ItemDisplayID));
	}
	for (size_t i=0; i<items.size(); ) {
		bool keepItem = (items[i].type==0) || (itemset.find(items[i].model)!=itemset.end());
		if (keepItem) {
			itemLookup[items[i].id] = (int)i;
			i++;
		}
		else items.erase(items.begin() + i);
	}
}

void ItemDatabase::cleanupDiscovery()
{
	for (size_t i=0; i<items.size(); ) {
		if (items[i].discovery)
			items.erase(items.begin() + i);
		else
			i++;
	}
}

int ItemDatabase::getItemIDByModel(int id)
{
	if (id == 0)
		return 0;
	for (std::vector<ItemRecord>::iterator it = items.begin(); it != items.end(); ++it)
		if(it->model == id) return it->id;
    
	return 0;
}

const ItemRecord& ItemDatabase::getById(int id)
{
    if (itemLookup.find(id)!=itemLookup.end()) 
		return items[itemLookup[id]];
	else 
		return items[0];
}

const ItemRecord& ItemDatabase::getByPos(int id)
{
	return items[id];
}

bool ItemDatabase::avaiable(int id)
{
	return (itemLookup.find(id)!=itemLookup.end());
/*
	for (std::vector<ItemRecord>::iterator it = items.begin(); it != items.end(); ++it)
		if(it->id == id) return id;

	return 0;
*/
}

int ItemDatabase::getItemNum(int displayid)
{
	for (std::vector<ItemRecord>::iterator it = items.begin(); it != items.end(); ++it)
		if(it->model == displayid) return it->id;
    
	return 0;
}

wxString ItemDatabase::addDiscoveryId(int id, wxString name)
{
	wxString ret = wxEmptyString;

	try {
		ItemDB::Record r = itemdb.getById(id);
		ItemRecord rec;
		rec.id = id;
		rec.model = r.getInt(ItemDB::ItemDisplayInfo);
		rec.itemclass = r.getInt(ItemDB::Itemclass);
		rec.subclass = r.getInt(ItemDB::Subclass);
		rec.type = r.getInt(ItemDB::InventorySlot);
		switch(r.getInt(ItemDB::Sheath)) {
			case SHEATHETYPE_MAINHAND: rec.sheath = ATT_LEFT_BACK_SHEATH; break;
			case SHEATHETYPE_LARGEWEAPON: rec.sheath = ATT_LEFT_BACK; break;
			case SHEATHETYPE_HIPWEAPON: rec.sheath = ATT_LEFT_HIP_SHEATH; break;
			case SHEATHETYPE_SHIELD: rec.sheath = ATT_MIDDLE_BACK_SHEATH; break;
			default: rec.sheath = SHEATHETYPE_NONE;
		}
		rec.discovery = true;
		rec.name.Printf(wxT("%s [%d] [%d]"), name.c_str(), rec.id, rec.model);
		if (rec.type > 0) {
			items.push_back(rec);
			itemLookup[rec.id] = (int)items.size()-1;
			//wxLogMessage(wxT("Info: Not exist ItemID: %d, %s..."), id, rec.name.c_str());
			ret.Printf(wxT("%d,%d,%d,%d,%d,%d,%d,%s"), rec.id, rec.model, rec.itemclass, rec.subclass,
				rec.type, rec.sheath, rec.quality, rec.name.c_str());
		}
	} catch (ItemDB::NotFound) {}
	return ret;
}

wxString ItemDatabase::addDiscoveryDisplayId(int id, wxString name, int type)
{
	wxString ret = wxEmptyString;

	ItemRecord rec;
	rec.id = id+ItemDB::MaxItem;
	rec.model = id;
	rec.itemclass = 4;
	rec.subclass = 0;
	rec.type = type;
	rec.sheath = 0;
	rec.discovery = true;
	rec.name.Printf(wxT("%s [%d]"), name.c_str(), id);
	if (rec.type > 0) {
		items.push_back(rec);
		itemLookup[rec.id] = (int)items.size()-1;
		//wxLogMessage(wxT("Info: Not exist ItemID: %d, %s..."), id, rec.name.c_str());
		ret.Printf(wxT("%d,%d,%d,%d,%d,%d,%d,%s"), rec.id, rec.model, rec.itemclass, rec.subclass,
			rec.type, rec.sheath, rec.quality, rec.name.c_str());
	}
	return ret;
}

ItemSubClassDB::Record ItemSubClassDB::getById(int id, int subid)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (gameVersion >= VERSION_CATACLYSM) {
			if (i->getInt(ClassIDV400)==id && i->getInt(SubClassIDV400)==subid)
				return (*i);
		} else {
			if (i->getInt(ClassID)==id && i->getInt(SubClassID)==subid)
				return (*i);
		}
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}
// ============================================================
// =============================================================

bool NPCDatabase::avaiable(int id)
{
	return (npcLookup.find(id)!=npcLookup.end());
/*
	for (std::vector<NPCRecord>::iterator it = npcs.begin(); it != npcs.end(); ++it)
		if(it->id == id) return it->id;
    
	return 0;
*/
}

wxString NPCDatabase::addDiscoveryId(int id, wxString name)
{
	wxString ret = wxEmptyString;

	NPCRecord rec;
	rec.id = id+100000;
	rec.model = id;
	rec.type = 7;
	rec.discovery = true;
	rec.name.Printf(wxT("%s [%d]"), name.c_str(), rec.id);
	if (rec.type > 0) {
		npcs.push_back(rec);
		ret.Printf(wxT("%d,%d,%d,%s"), rec.id, rec.model, rec.type, rec.name.c_str());
	}
	return ret;
}


NPCRecord::NPCRecord(wxString line)
    : id(0), model(0), type(0)
{
	if (line.Len() <= 3)
	    return;
	id = wxAtoi(line.BeforeFirst(','));
	line = line.AfterFirst(',');
	model = wxAtoi(line.BeforeFirst(','));
	line = line.AfterFirst(',');
	type = wxAtoi(line.BeforeFirst(','));
	line = line.AfterFirst(',');
	discovery = false;
	name.Printf(wxT("%s [%d] [%d]"), line.c_str(), id, model);
}

NPCDatabase::NPCDatabase(wxString filename)
{
	//ItemRecord all(wxT("---- None ----"), IT_ALL);
	//items.push_back(all);

	wxTextFile fin(filename);
	if (fin.Open(filename)) {
		wxString line;
		for ( line = fin.GetFirstLine(); !fin.Eof(); line = fin.GetNextLine() ) {
			NPCRecord rec(line);
			if (rec.model > 0) {
				npcs.push_back(rec);
			}
		}
		fin.Close();
		sort(npcs.begin(), npcs.end());
	}

	int j=0;
	for (std::vector<NPCRecord>::iterator it=npcs.begin();	it!=npcs.end(); ++it)
	{
		npcLookup[it->id] = j;
		j++;
	}
}

void NPCDatabase::open(wxString filename)
{
	wxTextFile fin(filename);
	if (fin.Open(filename)) {
		wxString line;
		for ( line = fin.GetFirstLine(); !fin.Eof(); line = fin.GetNextLine() ) {
			NPCRecord rec(line);
			if (rec.model > 0) {
				npcs.push_back(rec);
			}
		}
		fin.Close();
		sort(npcs.begin(), npcs.end());
	}
}


const NPCRecord& NPCDatabase::get(int id)
{
	return npcs[id];
}

const NPCRecord& NPCDatabase::getByID(int id)
{
    if (npcLookup.find(id)!=npcLookup.end()) {
		return npcs[npcLookup[id]];
    }
	
	return npcs[0];
/*
	for (std::vector<NPCRecord>::iterator it=npcs.begin();  it!=npcs.end(); ++it) {
		if (it->id == id) {
			return (*it);
		}
	}

	return npcs[0];
*/
}

// --



// --
// SPELLDB.H
//

/*

SpellVisualeffects.dbc
column 1 = id, int
column 2 = spell name, string
column 3 = model name, string
column 4 = number between 0 and 11, int
column 5 = number 0 or 1,  1 entry is 50?, int.. possibly boolean.

Column 3, ignore entries starting with "zzOLD__" ?
Column 4, wtf are .mdl files? they're from warcraft 3?

col 5 and 6? figure out what they're for.
Column5 is either Spell Type,  or Spell slot or something similar
*/
SpellEffectsDB::Record SpellEffectsDB::getByName(const wxString name)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (name.IsSameAs(i->getString(EffectName), false) == true)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

SpellEffectsDB::Record SpellEffectsDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(ID)==id)
			return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}
// --
