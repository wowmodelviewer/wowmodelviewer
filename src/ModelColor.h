/*
 * ModelColor.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#ifndef _MODELCOLOR_H_
#define _MODELCOLOR_H_

#include "animated.h"
#include "modelheaders.h"
#include "mpq.h"
#include "util.h" // uint32
#include "Vec3D.h"

class GameFile;

struct ModelColor {
	Animated<Vec3D> color;
	AnimatedShort opacity;

	void init(GameFile * f, ModelColorDef &mcd, uint32 *global);
};


#endif /* _MODELCOLOR_H_ */
