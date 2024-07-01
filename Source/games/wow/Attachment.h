/*
 * Attachment.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#pragma once

#include <string>
#include <vector>

#include "glm/glm.hpp"

class Displayable;
class WoWModel;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _ATTACHMENT_API_ __declspec(dllexport)
#    else
#        define _ATTACHMENT_API_ __declspec(dllimport)
#    endif
#else
#    define _ATTACHMENT_API_
#endif

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
