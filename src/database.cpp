#include "database.h"

#include "globalvars.h"
#include "modelviewer.h"

#include "logger/Logger.h"


ItemDatabase		items;
std::vector<NPCRecord> npcs;

// --
HelmGeosetDB		helmetdb;
ItemVisualEffectDB	effectdb;
ItemDisplayDB		itemdisplaydb;
StartOutfitDB		startdb;
ItemSubClassDB		subclassdb;
ItemVisualDB		visualdb;
ItemSetDB			setsdb;

// --
CharHairGeosetsDB	hairdb;
CharSectionsDB		chardb;
CharClassesDB		classdb;
CharFacialHairDB	facialhairdb;
CharRacesDB			racedb;
//--
NPCDB				npcdb;
LightSkyBoxDB			skyboxdb;
SpellItemEnchantmentDB	spellitemenchantmentdb;
ItemVisualsDB			itemvisualsdb;
CamCinematicDB		 camcinemadb;

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
	  if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(Section)==section) {
	    n++;
	  }
	}

    return n;
}

int CharSectionsDB::getSectionsFor(size_t race, size_t gender, size_t type, size_t color, size_t npc)
{
	int n = 0;
	for(Iterator i=begin(); i!=end(); ++i)
	{
	  if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(Color)==color) {
	    n++;
	  }
	}
    return n;
}

CharSectionsDB::Record CharSectionsDB::getByParams(size_t race, size_t gender, size_t type, size_t section, size_t color, size_t npc)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
	  if (i->getUInt(Race)==race && i->getUInt(Gender)==gender && i->getUInt(Type)==type && i->getUInt(Section)==section && i->getUInt(Color)==color)
	    return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d race:%d, gender:%d, type:%d, section:%d, color:%d"), __FILE__, __FUNCTION__, __LINE__, race, gender, type, section, color);
	throw NotFound();
}

// Races
CharRacesDB::Record CharRacesDB::getByName(wxString name)
{
	for(Iterator i=begin(); i!=end(); ++i) {
		wxString r = i->getString(Name);
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
  for(Iterator i=begin(); i!=end(); ++i)
  {
    if (i->getUInt(RaceV400)==race && i->getUInt(GenderV400)==gender && i->getUInt(StyleV400)==style)
      return (*i);
  }
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}

int CharFacialHairDB::getStylesFor(unsigned int race, unsigned int gender)
{
	int n = 0;
	for(Iterator i=begin(); i!=end(); ++i)
	{
	  if (i->getUInt(RaceV400)==race && i->getUInt(GenderV400)==gender) {
	    n++;
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
			int id = i->getUInt(ItemIDBaseV400+j);
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
ItemRecord::ItemRecord(const std::vector<std::string> & vals)
  : id(0), itemclass(0), subclass(0), type(0), model(0), sheath(0), quality(0)
{
  if(vals.size() < 7)
      return;

  id = atoi(vals[0].c_str());
  type = atoi(vals[2].c_str());
  itemclass = atoi(vals[3].c_str());
  subclass = atoi(vals[4].c_str());
  model = 1;
  quality = atoi(vals[6].c_str());
  switch(atoi(vals[5].c_str()))
  {
    case SHEATHETYPE_MAINHAND: sheath = ATT_LEFT_BACK_SHEATH; break;
    case SHEATHETYPE_LARGEWEAPON: sheath = ATT_LEFT_BACK; break;
    case SHEATHETYPE_HIPWEAPON: sheath = ATT_LEFT_HIP_SHEATH; break;
    case SHEATHETYPE_SHIELD: sheath = ATT_MIDDLE_BACK_SHEATH; break;
    default: sheath = SHEATHETYPE_NONE;
  }
  name = CSConv(wxString(vals[1].c_str(), wxConvUTF8));
}

// Alfred. prevent null items bug.
ItemDatabase::ItemDatabase()
{
	ItemRecord all;
	all.name=wxT("---- None ----");
	all.type=IT_ALL;

	items.push_back(all);
}

const ItemRecord& ItemDatabase::getById(int id)
{
  for (std::vector<ItemRecord>::iterator it=items.begin();  it!=items.end(); ++it)
  {
    if(it->id == id)
      return *it;
  }
  return items[0];
}

const ItemRecord& ItemDatabase::getByPos(int id)
{
	return items[id];
}

bool ItemDatabase::avaiable(int id)
{
	return (itemLookup.find(id)!=itemLookup.end());
}

int ItemDatabase::getItemNum(int displayid)
{
	for (std::vector<ItemRecord>::iterator it = items.begin(); it != items.end(); ++it)
		if(it->model == displayid) return it->id;
    
	return 0;
}


ItemSubClassDB::Record ItemSubClassDB::getById(int id, int subid)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
	  if (i->getInt(ClassIDV400)==id && i->getInt(SubClassIDV400)==subid)
	    return (*i);
	}
	//wxLogMessage(wxT("NotFound: %s:%s#%d"), __FILE__, __FUNCTION__, __LINE__);
	throw NotFound();
}
// ============================================================
// =============================================================

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
	name.Printf(wxT("%s [%d] [%d]"), line.c_str(), id, model);
}


NPCRecord::NPCRecord(const std::vector<std::string> & vals)
    : id(0), model(0), type(0)
{
  if(vals.size() < 4)
    return;

  id = atoi(vals[0].c_str());
  model = atoi(vals[1].c_str());
  type = atoi(vals[2].c_str());
  name = CSConv(wxString(vals[3].c_str(), wxConvUTF8));
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
