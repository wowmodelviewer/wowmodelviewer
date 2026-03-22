/*
 * ModelLight.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#pragma once

#include "animated.h"

class GameFile;

#include <glad/gl.h> // GLuint
#include "glm/glm.hpp"

/// @brief An animated light source attached to an M2 model bone.
struct ModelLight
{
	ssize_t type;   ///< Light type: 0 = directional, 1 = point.
	ssize_t parent; ///< Parent bone index (-1 if none).
	glm::vec3 pos, tpos, dir, tdir;  ///< Position, transformed position, direction, transformed direction.
	Animated<glm::vec3> diffColor, ambColor;  ///< Diffuse and ambient colour tracks.
	Animated<float> diffIntensity, ambIntensity, AttenStart, AttenEnd;  ///< Intensity and attenuation tracks.
	Animated<int> UseAttenuation;  ///< Whether attenuation is enabled.

	/// @brief Initialise from an M2 light definition block.
	void init(GameFile* f, ModelLightDef& mld, std::vector<uint32>& global);
	/// @brief Set up the OpenGL light for rendering.
	void setup(size_t time, GLuint l);
};
