/*
 * ModelTransparency.h
 *
 *  Created on: 22 oct. 2013
 *
 */

#ifndef _MODELTRANSPARENCY_H_
#define _MODELTRANSPARENCY_H_

#include "animated.h"
#include "modelheaders.h"

class GameFile;

struct ModelTransparency 
{
	AnimatedShort weight;

  void init(GameFile * f, M2TextureWeight &mtd, std::vector<uint32> & global);
};



#endif /* _MODELTRANSPARENCY_H_ */
