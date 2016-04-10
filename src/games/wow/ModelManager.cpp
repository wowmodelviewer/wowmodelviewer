/*
 * ModelManager.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "ModelManager.h"

#include "WoWModel.h"

// Adds models to the model manager, used by WMO's
int ModelManager::add(GameFile * file)
{
	int id;
	if (names.find(file->name()) != names.end()) {
    id = names[file->name()];
		items[id]->addref();
		return id;
	}
	// load new
	WoWModel *model = new WoWModel(file);
	id = nextID();
  do_add(file->name(), id, model);
    return id;
}

// Resets the animation back to default.
void ModelManager::resetAnim()
{
	for (std::map<int, ManagedItem*>::iterator it = items.begin(); it != items.end(); ++it) {
		((WoWModel*)it->second)->animcalc = false;
	}
}

// same as other updateEmitter except does it for the all the models being managed - for WMO's
void ModelManager::updateEmitters(float dt)
{
	for (std::map<int, ManagedItem*>::iterator it = items.begin(); it != items.end(); ++it) {
		((WoWModel*)it->second)->updateEmitters(dt);
	}
}

void ModelManager::clear()
{
	for (std::map<int, ManagedItem*>::iterator it = items.begin(); it != items.end(); ++it) {
		doDelete(it->first);
		delete it->second;
	}
	items.clear();
	names.clear();
}

