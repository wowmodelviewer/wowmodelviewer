/*
 * Bone.h
 *
 *  Created on: 19 oct. 2013
 *
 */

#ifndef _BONE_H_
#define _BONE_H_

#include "animated.h"
#include "enums.h" // int16
#include "matrix.h"
#include "modelheaders.h" // ModelBoneDef
#include "mpq.h" // MPQFile
#include "Vec3D.h"

class WoWModel;


class Bone {
public:
	Animated<Vec3D> trans;
	//Animated<Quaternion> rot;
	Animated<Quaternion, PACK_QUATERNION, Quat16ToQuat32> rot;
	Animated<Vec3D> scale;

	Vec3D pivot, transPivot;
	int16 parent;

	bool billboard;
	Matrix mat;
	Matrix mrot;

	ModelBoneDef boneDef;

	bool calc;
	WoWModel *model;
	void calcMatrix(Bone* allbones, ssize_t anim, size_t time, bool rotate=true);
	void initV3(GameFile & f, ModelBoneDef &b, uint32 *global, std::vector<GameFile *> &animfiles);
	void initV2(GameFile * f, ModelBoneDef &b, uint32 *global);
};


#endif /* _BONE_H_ */
