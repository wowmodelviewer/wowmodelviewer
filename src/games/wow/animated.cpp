/*
 * animated.cpp
 *
 *  Created on: 20 feb. 2015
 *      Author: Jeromnimo
 */


#include "animated.h"

size_t globalTime = 0;

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
