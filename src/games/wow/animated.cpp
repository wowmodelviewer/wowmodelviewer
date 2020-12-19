/*
 * animated.cpp
 *
 *  Created on: 20 feb. 2015
 *      Author: Jeromnimo
 */


#include "animated.h"

_ANIMATED_API_ size_t globalTime = 0;

glm::vec3 fixCoordSystem(glm::vec3 v)
{
  return glm::vec3(v.x, v.z, -v.y);
}

glm::vec3 fixCoordSystem2(glm::vec3 v)
{
  return glm::vec3(v.x, v.z, v.y);
}

glm::fquat fixCoordSystemQuat(glm::fquat v)
{
  return glm::fquat(v.w, -v.x, -v.z, v.y);
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
