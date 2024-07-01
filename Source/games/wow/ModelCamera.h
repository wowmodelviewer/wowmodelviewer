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

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _MODELCAMERA_API_ __declspec(dllexport)
#    else
#        define _MODELCAMERA_API_ __declspec(dllimport)
#    endif
#else
#    define _MODELCAMERA_API_
#endif

struct _MODELCAMERA_API_ ModelCamera
{
	bool ok;

	glm::vec3 pos, target;
	float nearclip, farclip, fov;
	Animated<glm::vec3> tPos, tTarget;
	Animated<float> rot;

	void init(GameFile* f, ModelCameraDef& mcd, std::vector<uint32>& global, std::string modelname);
	void initv10(GameFile* f, ModelCameraDefV10& mcd, std::vector<uint32>& global, std::string modelname);
	void setup(size_t time = 0);

	ModelCamera(): ok(false), pos(glm::vec3()), target(glm::vec3()),
	               nearclip(0), farclip(0), fov(0)
	{
	}
};
