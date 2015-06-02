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
class WoWModel;
class ModelCanvas;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _ATTACHMENT_API_ __declspec(dllexport)
#    else
#        define _ATTACHMENT_API_ __declspec(dllimport)
#    endif
#else
#    define _ATTACHMENT_API_
#endif

struct _ATTACHMENT_API_ Attachment {
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
	WoWModel* getModelFromSlot(int slot);

	void draw(ModelCanvas *c);
	void drawParticles(bool force=false);
	void tick(float dt);
};


#endif /* _ATTACHMENT_H_ */
