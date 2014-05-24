/*
 * ModelAttachment.h
 *
 *  Created on: 20 oct. 2013
 *
 */

#ifndef _MODELATTACHMENT_H_
#define _MODELATTACHMENT_H_

#include "Vec3D.h"
#include "modelheaders.h"
#include "mpq.h"
#include "util.h"


class WoWModel;

struct ModelAttachment {
	int id;
	Vec3D pos;
	int bone;
	WoWModel *model;

	void init(MPQFile &f, ModelAttachmentDef &mad, uint32 *global);
	void setup();
	void setupParticle();
};



#endif /* _MODELATTACHMENT_H_ */
