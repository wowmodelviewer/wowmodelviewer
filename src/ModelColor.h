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

struct ModelColor {
	Animated<Vec3D> color;
	AnimatedShort opacity;

	void init(MPQFile &f, ModelColorDef &mcd, uint32 *global);
};


#endif /* _MODELCOLOR_H_ */
