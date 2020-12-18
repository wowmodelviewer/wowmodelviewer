/*
 * TextureAnim.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#ifndef _TEXTUREANIM_H_
#define _TEXTUREANIM_H_

#include "animated.h"
#include "glm/glm.hpp"

class TextureAnim 
{
public:
	Animated<glm::vec3> trans, rot, scale;

	glm::vec3 tval, rval, sval;

	void calc(ssize_t anim, size_t time);
  void init(GameFile * f, ModelTexAnimDef &mta, std::vector<uint32> & global);
	void setup(ssize_t anim);
};


#endif /* _TEXTUREANIM_H_ */
