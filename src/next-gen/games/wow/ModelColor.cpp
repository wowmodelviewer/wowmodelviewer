/*
 * ModelColor.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "next-gen/games/wow/ModelColor.h"

#include "GameFile.h"

void ModelColor::init(GameFile * f, ModelColorDef &mcd, uint32 *global)
{
	color.init(mcd.color, f, global);
	opacity.init(mcd.opacity, f, global);
}


