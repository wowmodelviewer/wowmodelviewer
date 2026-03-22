/*
 * ModelColor.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#pragma once

#include "animated.h"
#include "modelheaders.h"

#include "glm/glm.hpp"

class GameFile;

/// @brief Animated vertex colour and opacity used by M2 render passes.
struct ModelColor
{
	Animated<glm::vec3> color;  ///< RGB colour animation track.
	AnimatedShort opacity;      ///< Opacity animation track (0 = transparent, 0x7FFF = opaque).

	/// @brief Initialise from an M2 colour definition block.
	void init(GameFile* f, ModelColorDef& mcd, std::vector<uint32>& global);
};
