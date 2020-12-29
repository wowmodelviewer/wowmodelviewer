/*
 * ModelColor.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#ifndef _MODELCOLOR_H_
#define _MODELCOLOR_H_

#include "animated.h"
#include "modelheaders.h"

#include "glm/glm.hpp"

class GameFile;

struct ModelColor 
{
  Animated<glm::vec3> color;
  AnimatedShort opacity;

  void init(GameFile * f, ModelColorDef &mcd, std::vector<uint32> & global);
};


#endif /* _MODELCOLOR_H_ */
