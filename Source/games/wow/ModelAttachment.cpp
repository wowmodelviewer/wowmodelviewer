/*
 * ModelAttachment.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "ModelAttachment.h"
#include "RenderList.h"

#include "Bone.h"
#include "WoWModel.h"

#include "glm/gtc/type_ptr.hpp"



void ModelAttachment::init(ModelAttachmentDef &mad)
{
  pos = mad.pos;
  bone = mad.bone;
  id = mad.id;
}

void ModelAttachment::setup() const
{
  if (wmv::RenderList * rl = wmv::RenderList::current()) rl->multiply(model->bones[bone].mat);
  if (wmv::RenderList * rl = wmv::RenderList::current()) rl->translate(pos);
}

void ModelAttachment::setupParticle() const
{
  if (wmv::RenderList * rl = wmv::RenderList::current()) rl->multiply(model->bones[bone].mat);
  if (wmv::RenderList * rl = wmv::RenderList::current()) rl->translate(pos);
}


