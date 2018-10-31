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

class TextureAnim
{
public:
  Animated<Vec3D> trans, rot, scale;

  Vec3D tval, rval, sval;

  void calc(int32 anim, uint32 time);
  void init(GameFile * f, ModelTexAnimDef &mta, QVector<uint32> & global);
  void setup(int anim);
};

#endif /* _TEXTUREANIM_H_ */