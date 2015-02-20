/*
 * animated.cpp
 *
 *  Created on: 20 feb. 2015
 *      Author: Jeromnimo
 */


#include "animated.h"

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

int randint(int lower, int upper)
{
    return lower + (int)((upper+1-lower)*frand());
}
