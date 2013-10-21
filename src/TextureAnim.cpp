/*
 * TextureAnim.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "TextureAnim.h"

#include "GL/glew.h"

void TextureAnim::calc(ssize_t anim, size_t time)
{
	if (trans.uses(anim)) {
		tval = trans.getValue(anim, time);
	}
	if (rot.uses(anim)) {
        rval = rot.getValue(anim, time);
	}
	if (scale.uses(anim)) {
       	sval = scale.getValue(anim, time);
	}
}

void TextureAnim::setup(ssize_t anim)
{
	glLoadIdentity();
	if (trans.uses(anim)) {
		glTranslatef(tval.x, tval.y, tval.z);
	}
	if (rot.uses(anim)) {
		glRotatef(rval.x, 0, 0, 1); // this is wrong, I have no idea what I'm doing here ;)
	}
	if (scale.uses(anim)) {
		glScalef(sval.x, sval.y, sval.z);
	}
}

void TextureAnim::init(MPQFile &f, ModelTexAnimDef &mta, uint32 *global)
{
	trans.init(mta.trans, f, global);
	rot.init(mta.rot, f, global);
	scale.init(mta.scale, f, global);
}


