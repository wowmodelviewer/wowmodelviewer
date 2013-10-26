/*
 * Attachment.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#ifndef _ATTACHMENT_H_
#define _ATTACHMENT_H_

#include "vec3d.h"

#include <vector>

#include <wx/string.h>

class Displayable;
class Model;
class ModelCanvas;

struct Attachment {
	Attachment *parent;
	Displayable *model;

	std::vector<Attachment*> children;

	int id;
	int slot;
	float scale;
	Vec3D rot;
	Vec3D pos;

	Attachment(Attachment *parent, Displayable *model, int id, int slot, float scale=1.0f, float rot=0.0f, Vec3D pos=Vec3D(0.0f, 0.0f, 0.0f)): parent(parent), model(model), id(id), slot(slot), scale(scale), rot(rot), pos(pos)
	{}

	~Attachment();

	void setup();
	void setupParticle();
	Attachment* addChild(wxString fn, int id, int slot, float scale=1.0f, float rot=0.0f, Vec3D pos=Vec3D(0.0f, 0.0f, 0.0f));
	Attachment* addChild(Displayable *disp, int id, int slot, float scale=1.0f, float rot=0.0f, Vec3D pos=Vec3D(0.0f, 0.0f, 0.0f));
	void delSlot(int slot);
	void delChildren();
	Model* getModelFromSlot(int slot);

	void draw(ModelCanvas *c);
	void drawParticles(bool force=false);
	void tick(float dt);
};


#endif /* _ATTACHMENT_H_ */
