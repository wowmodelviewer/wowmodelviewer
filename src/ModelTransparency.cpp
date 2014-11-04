/*
 * ModelTransparency.cpp
 *
 *  Created on: 22 oct. 2013
 *
 */

#include "ModelTransparency.h"

void ModelTransparency::init(GameFile * f, ModelTransDef &mcd, uint32 *global)
{
	trans.init(mcd.trans, f, global);
}

