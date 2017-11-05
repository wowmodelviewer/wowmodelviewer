/*
 * ModelColor.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "ModelColor.h"

#include "GameFile.h"

void ModelColor::init(GameFile * f, ModelColorDef &mcd, std::vector<uint32> & global)
{
	color.init(mcd.color, f, global);
	opacity.init(mcd.opacity, f, global);
}


