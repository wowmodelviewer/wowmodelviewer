/*
 * Attachment.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#ifndef _ATTACHMENT_H_
#define _ATTACHMENT_H_

#include <map>
#include <string>
#include <vector>
#include "vec3d.h"

class Displayable;
class WoWModel;
class BaseCanvas;

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
		Attachment(Attachment *parent, Displayable *model, int id, int slot, float scale=1.0f, float rot=0.0f, Vec3D pos=Vec3D(0.0f, 0.0f, 0.0f)): parent(parent), m_model(model), id(id), slot(slot), scale(scale), rot(rot), pos(pos)
		{}

		~Attachment();

		void setup();
		void setupParticle();
		Attachment* addChild(std::string fn, int id, int slot, float scale=1.0f, float rot=0.0f, Vec3D pos=Vec3D(0.0f, 0.0f, 0.0f));
		Attachment* addChild(Displayable *disp, int id, int slot, float scale=1.0f, float rot=0.0f, Vec3D pos=Vec3D(0.0f, 0.0f, 0.0f));
		void delSlot(int slot);
		void delChildren();
		WoWModel* getModelFromSlot(int slot);

		void draw(BaseCanvas *c);
		void drawParticles(bool force=false);
		void tick(float dt);

		void setModel(Displayable * newmodel);
		Displayable * model() { return m_model;}
		static Attachment * getAttachmentForModel(Displayable *);

		Attachment *parent;

		std::vector<Attachment*> children;

		int id;
		int slot;
		float scale;
		Vec3D rot;
		Vec3D pos;


	private:
		Displayable *m_model;
		static std::map<Displayable*,Attachment*> m_attMap;
};


#endif /* _ATTACHMENT_H_ */
