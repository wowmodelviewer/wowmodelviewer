/*
 * ModelTransparency.cpp
 *
 *  Created on: 22 oct. 2013
 *
 */

#include "ModelTransparency.h"

void ModelTransparency::init(GameFile * f, M2TextureWeight &mcd, std::vector<uint32> & global)
{
	weight.init(mcd.weight, f, global);
}

