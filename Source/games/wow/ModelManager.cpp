#include "ModelManager.h"
#include "WoWModel.h"

// Adds models to the model manager, used by WMO's
int ModelManager::add(GameFile* file)
{
	int id;
	if (names.find(file->name()) != names.end())
	{
		id = names[file->name()];
		items[id]->addref();
		return id;
	}
	// load new
	WoWModel* model = new WoWModel(file);
	id = nextID();
	do_add(file->name(), id, model);
	return id;
}

// Resets the animation back to default.
void ModelManager::resetAnim()
{
	for (const auto& item : items)
	{
		static_cast<WoWModel*>(item.second)->animcalc = false;
	}
}

// same as other updateEmitter except does it for the all the models being managed - for WMO's
void ModelManager::updateEmitters(float dt)
{
	for (const auto& item : items)
	{
		static_cast<WoWModel*>(item.second)->updateEmitters(dt);
	}
}

void ModelManager::clear()
{
	for (const auto& item : items)
	{
		doDelete(item.first);
		delete item.second;
	}
	items.clear();
	names.clear();
}
