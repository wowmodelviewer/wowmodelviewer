/*
 * TextureAnim.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#ifndef _TEXTUREANIM_H_
#define _TEXTUREANIM_H_

#include "animated.h"
#include "vec3d.h"

class TextureAnim {
public:
	Animated<Vec3D> trans, rot, scale;

	Vec3D tval, rval, sval;

	void calc(ssize_t anim, size_t time);
	void init(GameFile * f, ModelTexAnimDef &mta, uint32 *global);
	void setup(ssize_t anim);
};


#endif /* _TEXTUREANIM_H_ */
