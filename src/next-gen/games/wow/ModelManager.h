/*
 * ModelManager.h
 *
 *  Created on: 20 oct. 2013
 *
 */

#ifndef _MODELMANAGER_H_
#define _MODELMANAGER_H_

#include "manager.h"

class ModelManager: public SimpleManager {
public:
	int add(std::string name);

	ModelManager() : v(0) {}

	int v;

	void resetAnim();
	void updateEmitters(float dt);
	void clear();

};


#endif /* _MODELMANAGER_H_ */
