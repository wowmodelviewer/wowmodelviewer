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

#include "glm/glm.hpp"

struct ModelCamera 
{
  bool ok;

  glm::vec3 pos, target;
  float nearclip, farclip, fov;
  Animated<glm::vec3> tPos, tTarget;
  Animated<float> rot;

  void init(GameFile * f, M2Camera &mcd, std::vector<uint32> & global, std::string modelname);
  void setup(size_t time=0);

  ModelCamera():ok(false), pos(glm::vec3()), target(glm::vec3()),
      nearclip(0), farclip(0), fov(0) {}
};


#endif /* _MODELCAMERA_H_ */
