/*
 * ModelColor.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "ModelColor.h"

void ModelColor::init(MPQFile &f, ModelColorDef &mcd, uint32 *global)
{
	color.init(mcd.color, f, global);
	opacity.init(mcd.opacity, f, global);
}


