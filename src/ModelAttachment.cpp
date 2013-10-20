/*
 * ModelAttachment.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "ModelAttachment.h"

#include "GL/glew.h"

#include "Bone.h"
#include "matrix.h"
#include "Model.h"

void ModelAttachment::init(MPQFile &f, ModelAttachmentDef &mad, uint32 *global)
{
	pos = fixCoordSystem(mad.pos);
	bone = mad.bone;
	id = mad.id;
}

void ModelAttachment::setup()
{
	Matrix m = model->bones[bone].mat;
	m.transpose();
	glMultMatrixf(m);
	glTranslatef(pos.x, pos.y, pos.z);
}

void ModelAttachment::setupParticle()
{
	Matrix m = model->bones[bone].mat;
	m.transpose();
	glMultMatrixf(m);
	glTranslatef(pos.x, pos.y, pos.z);
}


