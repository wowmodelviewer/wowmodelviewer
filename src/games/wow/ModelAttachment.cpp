/*
 * ModelAttachment.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "ModelAttachment.h"

#include "Bone.h"
#include "WoWModel.h"
#include "GL/glew.h"

#include "glm/gtc/type_ptr.hpp"



void ModelAttachment::init(ModelAttachmentDef &mad)
{
  pos = fixCoordSystem(mad.pos);
  bone = mad.bone;
  id = mad.id;
}

void ModelAttachment::setup()
{
  glm::mat4 m = model->bones[bone].mat;
  m = glm::transpose(m);
  glMultMatrixf(glm::value_ptr(m));
  glTranslatef(pos.x, pos.y, pos.z);
}

void ModelAttachment::setupParticle()
{
  glm::mat4 m = model->bones[bone].mat;
  m = glm::transpose(m);
  glMultMatrixf(glm::value_ptr(m));
  glTranslatef(pos.x, pos.y, pos.z);
}


