/*
 * ModelManager.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "ModelManager.h"

#include "Model.h"

// Adds models to the model manager, used by WMO's
int ModelManager::add(wxString name)
{
	int id;
	if (names.find(name) != names.end()) {
		id = names[name];
		items[id]->addref();
		return id;
	}
	// load new
	Model *model = new Model(name);
	id = nextID();
    do_add(name, id, model);
    return id;
}

// Resets the animation back to default.
void ModelManager::resetAnim()
{
	for (std::map<int, ManagedItem*>::iterator it = items.begin(); it != items.end(); ++it) {
		((Model*)it->second)->animcalc = false;
	}
}

// same as other updateEmitter except does it for the all the models being managed - for WMO's
void ModelManager::updateEmitters(float dt)
{
	for (std::map<int, ManagedItem*>::iterator it = items.begin(); it != items.end(); ++it) {
		((Model*)it->second)->updateEmitters(dt);
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

