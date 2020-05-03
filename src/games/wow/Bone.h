/*
 * Bone.h
 *
 *  Created on: 19 oct. 2013
 *
 */

#ifndef _BONE_H_
#define _BONE_H_

#include "animated.h"
#include "matrix.h"
#include "modelheaders.h" // M2CompBone
#include "vec3d.h"
#include <M2Track.h>

class GameFile;
class WoWModel;



class Bone
{
public:
  Animated<Vec3D> transOld;
  Animated<Quaternion, PACK_QUATERNION, Quat16ToQuat32> rotOld;
  Animated<Vec3D> scaleOld;

  M2Track<Vec3D> trans;
	M2Track<Quaternion> rot;
  M2Track<Vec3D> scale;

  Vec3D pivot, transPivot;
	int16 parent;

	bool billboard;
	Matrix mat;
	Matrix mrot;

	M2CompBone boneDef;

	bool calc;
	void calcMatrix(std::vector<Bone> & allbones, ssize_t anim, size_t time);
  void initV3(GameFile & f, M2CompBone &b, const modelAnimData & data);
};


#endif /* _BONE_H_ */
