/*
 * ModelCamera.h
 *
 *  Created on: 20 oct. 2013
 *
 */

#pragma once

#include <string>
#include "animated.h"

#include "glm/glm.hpp"

#define _MODELCAMERA_API_

/// @brief An animated camera defined within an M2 model (e.g. portrait or character-info camera).
struct _MODELCAMERA_API_ ModelCamera
{
	bool ok;  ///< Whether this camera was successfully initialised.

	glm::vec3 pos, target;  ///< Camera position and look-at target.
	float nearclip, farclip, fov;  ///< Clipping planes and field of view.
	Animated<glm::vec3> tPos, tTarget;  ///< Animated position and target tracks.
	Animated<float> rot;  ///< Animated roll track.

	/// @brief Initialise from an M2 camera definition block.
	void init(GameFile* f, ModelCameraDef& mcd, std::vector<uint32>& global, std::string modelname);
	/// @brief Initialise from a v10 camera definition block.
	void initv10(GameFile* f, ModelCameraDefV10& mcd, std::vector<uint32>& global, std::string modelname);
	/// @brief Apply the camera transform at the given time.
	void setup(size_t time = 0);

	ModelCamera() : ok(false), pos(glm::vec3()), target(glm::vec3()), nearclip(0), farclip(0),
					fov(0), tPos(), tTarget(), rot()
	{
	}
};
