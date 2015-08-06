#include "database.h"

#include "globalvars.h"
#include "modelviewer.h"

#include "logger/Logger.h"


_DATABASE_API_ ItemDatabase		items;
_DATABASE_API_ std::vector<NPCRecord> npcs;

// --
_DATABASE_API_ HelmGeosetDB		helmetdb;
_DATABASE_API_ StartOutfitDB		startdb;
_DATABASE_API_ ItemSetDB			setsdb;

//--
_DATABASE_API_ LightSkyBoxDB			skyboxdb;
_DATABASE_API_ CamCinematicDB		 camcinemadb;

// --
// CAMCINEMADB.H
CamCinematicDB::Record CamCinematicDB::getByCamModel(std::string fn)
{
	LOG_INFO << "Searching for CamModel...";
	// Brute force search for now
	for (Iterator i=begin(); i!=end(); ++i)
	{
		// to repair
		/*
		wxString str(i->getString(CamModel));
		LOG_INFO << "CamModel:" << str.c_str() << "VS" <<  fn.c_str();
		if(str.IsSameAs(fn, false) == true)
			return (*i);
			*/
	}
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
	throw NotFound();
}
// --


// --
// ITEMDB.H
//
// --------------------------------
// Item Database Stuff
// --------------------------------

ItemSetDB::Record ItemSetDB::getById(unsigned int id)
{
	for(Iterator i=begin(); i!=end(); ++i)
	{
		if (i->getUInt(SetID)==id)
			return (*i);
	}
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
	throw NotFound();
}



////////////////////
ItemRecord::ItemRecord(const std::vector<QString> & vals)
  : id(0), itemclass(0), subclass(0), type(0), model(0), sheath(0), quality(0)
{
  if(vals.size() < 7)
      return;

  id = vals[0].toInt();
  type = vals[2].toInt();
  itemclass = vals[3].toInt();
  subclass = vals[4].toInt();
  model = 1;
  quality = vals[6].toInt();
  switch(vals[5].toInt())
  {
    case SHEATHETYPE_MAINHAND: sheath = ATT_LEFT_BACK_SHEATH; break;
    case SHEATHETYPE_LARGEWEAPON: sheath = ATT_LEFT_BACK; break;
    case SHEATHETYPE_HIPWEAPON: sheath = ATT_LEFT_HIP_SHEATH; break;
    case SHEATHETYPE_SHIELD: sheath = ATT_MIDDLE_BACK_SHEATH; break;
    default: sheath = SHEATHETYPE_NONE;
  }
  name = vals[1];
}

// Alfred. prevent null items bug.
ItemDatabase::ItemDatabase()
{
	ItemRecord all;
	all.name= "---- None ----";
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

// ============================================================
// =============================================================

NPCRecord::NPCRecord(QString line)
    : id(0), model(0), type(0)
{
	QStringList values = line.split(',');

	if (values.size() <= 3)
	    return;

	id = values[0].toInt();
	model = values[1].toInt();
	type = values[2].toInt();
	name = values[3];
}


NPCRecord::NPCRecord(const std::vector<QString> & vals)
    : id(0), model(0), type(0)
{
  if(vals.size() < 4)
    return;

  id = vals[0].toInt();
  model = vals[1].toInt();
  type = vals[2].toInt();
  name = vals[3];
}
