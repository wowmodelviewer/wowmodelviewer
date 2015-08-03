/*
 * Attachment.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "next-gen/games/wow/Attachment.h"
#include "logger/Logger.h"

#include <string>

#include "modelcanvas.h"

Attachment::~Attachment()
{
	delChildren();

	parent = NULL;
	wxDELETE(model);
}

void Attachment::draw(ModelCanvas *c)
{
	if (!c)
		return;

	glPushMatrix();

	if (model) {
		//model->reset();
		setup();

		WoWModel *m = static_cast<WoWModel*>(model);

		if (!m) {
			glPopMatrix();
			return;
		}

		if (c->model) {
			// no need to scale if its already 100%
			// scaling manually set from model control panel
			if (scale != 1.0f)
				glScalef(scale, scale, scale);

			if (pos != Vec3D(0.0f, 0.0f, 0.0f))
				glTranslatef(pos.x, pos.y, pos.z);

			if (rot != Vec3D(0.0f,0.0f,0.0f)) {
				glRotatef(rot.x, 1.0f, 0.0f, 0.0f);
				glRotatef(rot.y, 0.0f, 1.0f, 0.0f);
				glRotatef(rot.z, 0.0f, 0.0f, 1.0f);
			}


			if (m->showModel && (m->alpha!=1.0f)) {
				glDisable(GL_COLOR_MATERIAL);

				float a[] = {1.0f, 1.0f, 1.0f, m->alpha};
				glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, a);

				glEnable(GL_BLEND);
				//glDisable(GL_DEPTH_TEST);
				//glDepthMask(GL_FALSE);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}

			if (!m->showTexture || video.useMasking)
				glDisable(GL_TEXTURE_2D);
			else
				glEnable(GL_TEXTURE_2D);
		}

		// shift or rotate the attached model
		if (c->model && c->model != m) {
			if (m->pos != Vec3D(0.0f, 0.0f, 0.0f))
				glTranslatef(m->pos.x, m->pos.y, m->pos.z);

			if (m->rot != Vec3D(0.0f,0.0f,0.0f)) {
				glRotatef(m->rot.x, 1.0f, 0.0f, 0.0f);
				glRotatef(m->rot.y, 0.0f, 1.0f, 0.0f);
				glRotatef(m->rot.z, 0.0f, 0.0f, 1.0f);
			}
		}

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		// We call this no matter what so that the model will still 'animate'.
		// and we do the 'showmodel' check inside the function
		model->draw();

		if (c->model) {
			if (m->showModel && (m->alpha!=1.0f)) {
				float a[] = {1.0f, 1.0f, 1.0f, 1.0f};
				glMaterialfv(GL_FRONT, GL_DIFFUSE, a);

				glDisable(GL_BLEND);
				//glEnable(GL_DEPTH_TEST);
				//glDepthMask(GL_TRUE);
				glEnable(GL_COLOR_MATERIAL);
			}

			if (!video.useMasking) {
				glDisable(GL_LIGHTING);
				glDisable(GL_TEXTURE_2D);

				if (m->showBounds)
					m->drawBoundingVolume();

				if (m->showBones)
					m->drawBones();

				glEnable(GL_LIGHTING);
			}
		}
	}

	// children:
	for (size_t i=0; i<children.size(); i++)
		children[i]->draw(c);

	glPopMatrix();
}

void Attachment::drawParticles(bool force)
{
	glPushMatrix();

	if (model) {
		WoWModel *m = static_cast<WoWModel*>(model);
		if (!m) {
			glPopMatrix();
			return;
		}

		model->reset();
		setupParticle();

		// no need to scale if its already 100%
		if (scale != 1.0f)
			glScalef(scale, scale, scale);

		if (rot != Vec3D(0.0f, 0.0f, 0.0f))
			glRotatef(rot.y, 0.0f, 1.0f, 0.0f);

		//glRotatef(45.0f, 1,0,0);

		if (pos != Vec3D(0.0f, 0.0f, 0.0f))
			glTranslatef(pos.x, pos.y, pos.z);

		if (force)
			m->drawParticles();
		else if (m->hasParticles && m->showParticles)
			m->drawParticles();

	}

	// children:
	for (size_t i=0; i<children.size(); i++)
		children[i]->drawParticles();

	glPopMatrix();
}



void Attachment::tick(float dt)
{
	if (model)
		model->update(dt);
	for (size_t i=0; i<children.size(); i++)
		children[i]->tick(dt);
}

void Attachment::setup()
{
	if (parent==0)
		return;
	if (parent->model)
		parent->model->setupAtt(id);
}

void Attachment::setupParticle()
{
	if (parent==0)
		return;
	if (parent->model)
		parent->model->setupAtt2(id);
}

Attachment* Attachment::addChild(std::string modelfn, int id, int slot, float scale, float rot, Vec3D pos)
{
	if (modelfn.length() == 0 || id<0)
		return 0;

	WoWModel *m = new WoWModel(modelfn, true);

	if (m && m->ok) {
		return addChild(m, id, slot, scale, rot, pos);
	} else {
		wxDELETE(m);
		return 0;
	}
}

Attachment* Attachment::addChild(Displayable *disp, int id, int slot, float scale, float rot, Vec3D pos)
{
	LOG_INFO << "Attach on id" << id << "slot" << slot << "scale" << scale << "rot" << rot << "pos (" << pos.x << "," << pos.y << "," << pos.z << ")";
	Attachment *att = new Attachment(this, disp, id, slot, scale, rot, pos);
	children.push_back(att);
	return att;
}

void Attachment::delChildren()
{
	for (size_t i=0; i<children.size(); i++) {
		children[i]->delChildren();
		wxDELETE(children[i]);
	}

	children.clear();
}

void Attachment::delSlot(int slot)
{
	for (size_t i=0; i<children.size(); ) {
		if (children[i]->slot == slot) {
			children.erase(children.begin() + i);
		} else i++;
	}
}

WoWModel* Attachment::getModelFromSlot(int slot)
{
	for (size_t i=0; i<children.size(); ) {
		if (children[i]->slot == slot) {
			return (static_cast<WoWModel*>(children[i]->model));
		} else i++;
	}

	return NULL;
}

