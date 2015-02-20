/*
 * ModelLight.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "ModelLight.h"

#include "logger/Logger.h"

#include "enums.h"


void ModelLight::init(GameFile * f, ModelLightDef &mld, uint32 *global)
{
	tpos = pos = fixCoordSystem(mld.pos);
	tdir = dir = Vec3D(0,1,0); // no idea
	type = mld.type;
	parent = mld.bone;
	ambColor.init(mld.ambientColor, f, global);
	ambIntensity.init(mld.ambientIntensity, f, global);
	diffColor.init(mld.diffuseColor, f, global);
	diffIntensity.init(mld.diffuseIntensity, f, global);
	AttenStart.init(mld.attenuationStart, f, global);
	AttenEnd.init(mld.attenuationEnd, f, global);
	UseAttenuation.init(mld.useAttenuation, f, global);
}

void ModelLight::setup(size_t time, GLuint l)
{
	Vec4D ambcol(ambColor.getValue(0, time) * ambIntensity.getValue(0, time), 1.0f);
	Vec4D diffcol(diffColor.getValue(0, time) * diffIntensity.getValue(0, time), 1.0f);
	Vec4D p;
	if (type==MODELLIGHT_DIRECTIONAL) {
		// directional
		p = Vec4D(tdir, 0.0f);
	} else if (type==MODELLIGHT_POINT) {
		// point
		p = Vec4D(tpos, 1.0f);
	} else {
		p = Vec4D(tpos, 1.0f);
		wxLogMessage(wxT("Error: Light type %d is unknown."), type);
	}
	//gLog("Light %d (%f,%f,%f) (%f,%f,%f) [%f,%f,%f]\n", l-GL_LIGHT4, ambcol.x, ambcol.y, ambcol.z, diffcol.x, diffcol.y, diffcol.z, p.x, p.y, p.z);
	glLightfv(l, GL_POSITION, p);
	glLightfv(l, GL_DIFFUSE, diffcol);
	glLightfv(l, GL_AMBIENT, ambcol);
	glEnable(l);
}
