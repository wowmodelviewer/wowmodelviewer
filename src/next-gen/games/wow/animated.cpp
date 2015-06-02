/*
 * animated.cpp
 *
 *  Created on: 20 feb. 2015
 *      Author: Jeromnimo
 */


#include "animated.h"

_ANIMATED_API_ size_t globalTime = 0;

Vec3D fixCoordSystem(Vec3D v)
{
  return Vec3D(v.x, v.z, -v.y);
}

Vec3D fixCoordSystem2(Vec3D v)
{
  return Vec3D(v.x, v.z, v.y);
}

Quaternion fixCoordSystemQuat(Quaternion v)
{
  return Quaternion(-v.x, -v.z, v.y, v.w);
}

float frand()
{
    return rand()/(float)RAND_MAX;
}

float randfloat(float lower, float upper)
{
  return lower + (upper-lower)*(rand()/(float)RAND_MAX);
}

_ANIMATED_API_ int randint(int lower, int upper)
{
    return lower + (int)((upper+1-lower)*frand());
}
