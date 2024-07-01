/*
 * ModelAttachment.h
 *
 *  Created on: 20 oct. 2013
 *
 */

#pragma once

#include "modelheaders.h"

#include "glm/glm.hpp"

class GameFile;
class WoWModel;

struct ModelAttachment
{
	int id;
	glm::vec3 pos;
	int bone;
	WoWModel* model;

	void init(ModelAttachmentDef& mad);
	void setup() const;
	void setupParticle() const;
};
