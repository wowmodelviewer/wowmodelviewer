/*
 * ModelLight.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#ifndef _MODELLIGHT_H_
#define _MODELLIGHT_H_

#include "animated.h"
#include "vec3d.h"

class GameFile;

#include "GL/glew.h" // GLuint

struct ModelLight 
{
  int32 type;		// Light Type. MODELLIGHT_DIRECTIONAL = 0 or MODELLIGHT_POINT = 1
  int32 parent;		// Bone Parent. -1 if there isn't one.
	Vec3D pos, tpos, dir, tdir;
	Animated<Vec3D> diffColor, ambColor;
	Animated<float> diffIntensity, ambIntensity, AttenStart, AttenEnd;
	Animated<int> UseAttenuation;

  void init(GameFile * f, ModelLightDef &mld, QVector<uint32> & global);
	void setup(uint32 time, GLuint l);
};


#endif /* _MODELLIGHT_H_ */
