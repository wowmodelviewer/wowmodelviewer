#include "animated.h"

size_t globalTime = 0;

float frand()
{
	return rand() / static_cast<float>(RAND_MAX);
}

float randfloat(float lower, float upper)
{
	return lower + (upper - lower) * (rand() / static_cast<float>(RAND_MAX));
}

int randint(int lower, int upper)
{
	return lower + static_cast<int>((upper + 1 - lower) * frand());
}
