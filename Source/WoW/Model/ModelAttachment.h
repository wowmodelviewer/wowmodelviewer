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

/// @brief An attachment point on a WoW model (weapon, shoulder, helmet, etc.).
struct ModelAttachment
{
	int id;            ///< Attachment slot ID (see POSITION_SLOTS).
	glm::vec3 pos;     ///< Position relative to the parent bone.
	int bone;          ///< Index of the bone this attachment is parented to.
	WoWModel* model;   ///< The model this attachment belongs to.

	/// @brief Initialise from an on-disk attachment definition.
	void init(ModelAttachmentDef& mad);
	/// @brief Set up the attachment transform for rendering.
	void setup() const;
};
