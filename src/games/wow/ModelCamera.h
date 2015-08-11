/*
 * ModelCamera.h
 *
 *  Created on: 20 oct. 2013
 *
 */

#ifndef _MODELCAMERA_H_
#define _MODELCAMERA_H_

#include <string>
#include "animated.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _MODELCAMERA_API_ __declspec(dllexport)
#    else
#        define _MODELCAMERA_API_ __declspec(dllimport)
#    endif
#else
#    define _MODELCAMERA_API_
#endif

struct _MODELCAMERA_API_ ModelCamera {
	bool ok;

	Vec3D pos, target;
	float nearclip, farclip, fov;
	Animated<Vec3D> tPos, tTarget;
	Animated<float> rot;
	Vec3D WorldOffset;
	float WorldRotation;

	void init(GameFile * f, ModelCameraDef &mcd, uint32 *global, std::string modelname);
	void initv10(GameFile * f, ModelCameraDefV10 &mcd, uint32 *global, std::string modelname);
	void setup(size_t time=0);

	ModelCamera():ok(false) {}
};


#endif /* _MODELCAMERA_H_ */
