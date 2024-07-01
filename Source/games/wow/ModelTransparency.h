#pragma once

#include "animated.h"
#include "modelheaders.h"

class GameFile;

struct ModelTransparency
{
	AnimatedShort trans;

	void init(GameFile* f, ModelTransDef& mtd, std::vector<uint32>& global);
};
