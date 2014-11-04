/*
 * ModelCamera.h
 *
 *  Created on: 20 oct. 2013
 *
 */

#ifndef _MODELCAMERA_H_
#define _MODELCAMERA_H_

#include "animated.h"

struct ModelCamera {
	bool ok;

	Vec3D pos, target;
	float nearclip, farclip, fov;
	Animated<Vec3D> tPos, tTarget;
	Animated<float> rot;
	Vec3D WorldOffset;
	float WorldRotation;

	void init(GameFile * f, ModelCameraDef &mcd, uint32 *global, wxString modelname);
	void initv10(GameFile * f, ModelCameraDefV10 &mcd, uint32 *global, wxString modelname);
	void setup(size_t time=0);

	ModelCamera():ok(false) {}
};


#endif /* _MODELCAMERA_H_ */
