/*
 * Attachment.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#pragma once

#include <string>
#include <vector>

class Displayable;
class WoWModel;

#define _ATTACHMENT_API_

/// @brief Scene-graph node that attaches a Displayable to a parent bone slot.
///
/// Forms a tree of model attachments (e.g. character → weapon → enchant glow).
/// Each node owns its children and delegates draw/tick calls down the tree.
class _ATTACHMENT_API_ Attachment
{
public:
	Attachment(Attachment* parent, Displayable* model, int id, int slot);
	~Attachment();

	void setup();
	void setupParticle();
	Attachment* addChild(std::string fn, int id, int slot);
	Attachment* addChild(Displayable* disp, int id, int slot);
	void delSlot(int slot);
	void delChildren();

	void draw();
	void drawParticles();
	void tick(float dt);

	void setModel(Displayable* newmodel);
	Displayable* model() const { return model_; }

	Attachment* parent;

	std::vector<Attachment*> children;

	int id;
	int slot;

private:
	Displayable* model_;
};
