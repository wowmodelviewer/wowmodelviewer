#pragma once

#include "animated.h"
#include "glm/glm.hpp"

/// @brief Animated texture transform (translation, rotation, scale) for an M2 model.
class TextureAnim
{
public:
	Animated<glm::vec3> trans, rot, scale;  ///< Animation tracks for translation, rotation, and scale.

	glm::vec3 tval, rval, sval;  ///< Current computed values.

	/// @brief Compute the transform at the given animation frame.
	void calc(ssize_t anim, size_t time);
	/// @brief Initialise from an M2 texture animation definition.
	void init(GameFile* f, ModelTexAnimDef& mta, std::vector<uint32>& global);
	/// @brief Apply the current texture transform to the OpenGL matrix stack.
	void setup(ssize_t anim);
};
