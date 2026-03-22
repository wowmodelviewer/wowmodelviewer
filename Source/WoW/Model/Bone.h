/*
 * Bone.h
 *
 *  Created on: 19 oct. 2013
 *
 */

#pragma once

#include "animated.h"
#include "modelheaders.h" // ModelBoneDef

#include "glm/glm.hpp"

class GameFile;
class WoWModel;

/// @brief Represents a single bone in a WoW M2 model skeleton.
class Bone
{
public:
	Animated<glm::vec3> trans;  ///< Translation animation track.
	Animated<glm::fquat, PACK_QUATERNION, Quat16ToQuat32> rot;  ///< Rotation animation track (packed 16-bit).
	Animated<glm::vec3> scale;  ///< Scale animation track.

	glm::vec3 pivot, transPivot;  ///< Pivot point and translated pivot.
	int16 parent;  ///< Parent bone index (-1 if root).

	bool billboard;  ///< Whether this bone is billboarded.
	glm::mat4 mat;   ///< Computed transformation matrix.
	glm::mat4 mrot;  ///< Computed rotation-only matrix.

	ModelBoneDef boneDef;  ///< Raw bone definition from the M2 file.

	bool calc;  ///< Whether this bone has been calculated for the current frame.

	/// @brief Compute the bone matrix from parent bones and animation data.
	void calcMatrix(std::vector<Bone>& allbones, ssize_t anim, size_t time, bool rotate = true);
	/// @brief Initialise the bone from an M2 v3+ file.
	void initV3(GameFile& f, ModelBoneDef& b, const modelAnimData& data);
};
