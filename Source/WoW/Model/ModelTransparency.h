#pragma once

#include "animated.h"
#include "modelheaders.h"

class GameFile;

/// @brief Animated transparency value for an M2 model texture layer.
struct ModelTransparency
{
	AnimatedShort trans;  ///< Transparency animation track (0 = transparent, 0x7FFF = opaque).

	/// @brief Initialise from an M2 transparency definition block.
	void init(GameFile* f, ModelTransDef& mtd, std::vector<uint32>& global);
};
