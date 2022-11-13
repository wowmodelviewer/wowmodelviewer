/*
 * Bone.h
 *
 *  Created on: 19 oct. 2013
 *
 */

#ifndef _BONE_H_
#define _BONE_H_

#include "animated.h"
#include "modelheaders.h" // ModelBoneDef

#include "glm/glm.hpp"

class GameFile;
class WoWModel;



class Bone {
public:
  Animated<glm::vec3> trans;
  Animated<glm::fquat, PACK_QUATERNION, Quat16ToQuat32> rot;
  Animated<glm::vec3> scale;

  glm::vec3 pivot, transPivot;
  int16 parent;

  bool billboard;
  glm::mat4 mat;
  glm::mat4 mrot;

  ModelBoneDef boneDef;

  bool calc;
  void calcMatrix(std::vector<Bone> & allbones, ssize_t anim, size_t time, bool rotate=true);
  void initV3(GameFile & f, ModelBoneDef &b, const modelAnimData & data);
};


#endif /* _BONE_H_ */
