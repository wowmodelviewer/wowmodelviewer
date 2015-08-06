/*
 * ModelManager.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "next-gen/games/wow/ModelManager.h"
#include "WoWModel.h"

// Adds models to the model manager, used by WMO's
int ModelManager::add(std::string name)
{
	int id;
	if (names.find(name) != names.end()) {
		id = names[name];
		items[id]->addref();
		return id;
	}
	// load new
	WoWModel *model = new WoWModel(name);
	id = nextID();
    do_add(name, id, model);
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

