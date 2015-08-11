/*
 * ModelAttachment.h
 *
 *  Created on: 20 oct. 2013
 *
 */

#ifndef _MODELATTACHMENT_H_
#define _MODELATTACHMENT_H_

#include "modelheaders.h"
#include "vec3d.h"

class GameFile;
class WoWModel;

struct ModelAttachment {
	int id;
	Vec3D pos;
	int bone;
	WoWModel *model;

	void init(GameFile *f, ModelAttachmentDef &mad, uint32 *global);
	void setup();
	void setupParticle();
};



#endif /* _MODELATTACHMENT_H_ */
