#pragma once

#include "manager.h"

class ModelManager : public SimpleManager
{
public:
	int add(GameFile*);

	ModelManager() : v(0)
	{
	}

	int v;

	void resetAnim();
	void updateEmitters(float dt);
	void clear();
};
