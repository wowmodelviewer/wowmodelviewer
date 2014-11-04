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
#include "mpq.h"
#include "util.h"

struct ModelTransparency {
	AnimatedShort trans;

	void init(GameFile * f, ModelTransDef &mtd, uint32 *global);
};



#endif /* _MODELTRANSPARENCY_H_ */
