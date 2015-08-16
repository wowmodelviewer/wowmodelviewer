#include "database.h"

#include "globalvars.h"
#include "wow_enums.h"
#include "logger/Logger.h"


_DATABASE_API_ ItemDatabase		items;
_DATABASE_API_ std::vector<NPCRecord> npcs;

// --
_DATABASE_API_ HelmGeosetDB		helmetdb;

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

int ItemRecord::slot()
{
	switch (type)
	{
		case IT_HEAD:
			return CS_HEAD;
		case IT_SHOULDER:
			return CS_SHOULDER;
		case IT_SHIRT:
			return CS_SHIRT;
		case IT_CHEST:
		case IT_ROBE:
			return CS_CHEST;
		case IT_BELT:
			return CS_BELT;
		case IT_PANTS:
			return CS_PANTS;
		case IT_BOOTS:
			return CS_BOOTS;
		case IT_BRACERS:
			return CS_BRACERS;
		case IT_GLOVES:
			return CS_GLOVES;
		case IT_DAGGER:
		case IT_RIGHTHANDED:
		case IT_GUN:
		case IT_THROWN:
		case IT_2HANDED:
		case IT_BOW:
			return CS_HAND_RIGHT;
		case IT_SHIELD:
		case IT_LEFTHANDED:
		case IT_OFFHAND:
			return CS_HAND_LEFT;
		case IT_CAPE:
			return CS_CAPE;
		case IT_TABARD:
			return CS_TABARD;
		case IT_RINGS:
		case IT_ACCESSORY:
		case IT_QUIVER:
		case IT_AMMO:
		case IT_UNUSED:
		case IT_RELIC:
		case IT_NECK:
		default:
			return -1;
	}
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
