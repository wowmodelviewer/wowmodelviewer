/*
 * ModelCamera.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "ModelCamera.h"

#include "GL/glew.h"

#include "database.h"
#include "video.h" // video global var

#include "logger/Logger.h"

void ModelCamera::init(GameFile * f, ModelCameraDef &mcd, uint32 *global, wxString modelname)
{
	wxLogMessage(wxT("Using original Camera Model Definitions."));
	ok = true;
    nearclip = mcd.nearclip;
	farclip = mcd.farclip;
	fov = mcd.fov;
	pos = fixCoordSystem(mcd.pos);
	target = fixCoordSystem(mcd.target);
	tPos.init(mcd.transPos, f, global);
	tTarget.init(mcd.transTarget, f, global);
	rot.init(mcd.rot, f, global);
	tPos.fix(fixCoordSystem);
	tTarget.fix(fixCoordSystem);

	Vec3D wopos = Vec3D(0,0,0);
	float worot = 0.0f;
	if (modelname.Find(wxT("Cameras\\"))>-1) {
		try {
			wxLogMessage(wxT("Trying Camera DB..."));
			wxString mn = modelname.BeforeLast(wxT('.')) + wxT(".mdx");
			wxLogMessage(wxT("ModelName: %s"), mn.c_str());
			CamCinematicDB::Record r = camcinemadb.getByCamModel(mn.c_str());
			wxLogMessage(wxT("Setting variables.."));
			wopos = fixCoordSystem(Vec3D(r.getFloat(camcinemadb.PosX),r.getFloat(camcinemadb.PosY),r.getFloat(camcinemadb.PosZ)));
			worot = r.getFloat(camcinemadb.Rot);
		}
		catch (CamCinematicDB::NotFound) {
			wxLogMessage(wxT("DBFilesClient/CinematicCamera.dbc not found."));
			wopos = Vec3D(0,0,0);
			worot = 0.0f;
		}
		wxLogMessage(wxT("WorldPos: %f, %f, %f"), wopos.x, wopos.y, wopos.z);
		wxLogMessage(wxT("WorldRot: %f"), worot);
	}
	WorldOffset = fixCoordSystem(wopos);
	WorldRotation = worot;
}

void ModelCamera::initv10(GameFile * f, ModelCameraDefV10 &mcd, uint32 *global, wxString modelname)
{
	wxLogMessage(wxT("Using version 10 Camera Model Definitions."));
	ok = true;
    nearclip = mcd.nearclip;
	farclip = mcd.farclip;
	pos = fixCoordSystem(mcd.pos);
	target = fixCoordSystem(mcd.target);
	tPos.init(mcd.transPos, f, global);
	tTarget.init(mcd.transTarget, f, global);
	rot.init(mcd.rot, f, global);
	tPos.fix(fixCoordSystem);
	tTarget.fix(fixCoordSystem);
	fov = 0.95f;

	Vec3D wopos = Vec3D(0,0,0);
	float worot = 0.0f;
	if (modelname.Find(wxT("Cameras\\"))>-1) {
		try {
			wxLogMessage(wxT("Trying Camera DB..."));
			wxString mn = modelname.BeforeLast(wxT('.')) + wxT(".mdx");
			wxLogMessage(wxT("ModelName: %s"), mn.c_str());
			CamCinematicDB::Record r = camcinemadb.getByCamModel(mn.c_str());
			wxLogMessage(wxT("Setting variables.."));
			wopos = fixCoordSystem(Vec3D(r.getFloat(camcinemadb.PosX),r.getFloat(camcinemadb.PosY),r.getFloat(camcinemadb.PosZ)));
			worot = r.getFloat(camcinemadb.Rot);
		}
		catch (CamCinematicDB::NotFound) {
			wxLogMessage(wxT("DBFilesClient/CinematicCamera.dbc not found."));
			wopos = Vec3D(0,0,0);
			worot = 0.0f;
		}
		wxLogMessage(wxT("WorldPos: %f, %f, %f"), wopos.x, wopos.y, wopos.z);
		wxLogMessage(wxT("WorldRot: %f"), worot);
	}
	WorldOffset = fixCoordSystem(wopos);
	WorldRotation = worot;
}

void ModelCamera::setup(size_t time)
{
	if (!ok) return;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(fov * 34.5f, (GLfloat)video.xRes/(GLfloat)video.yRes, nearclip, farclip*5);

	Vec3D p = pos + tPos.getValue(0, time);
	Vec3D t = target + tTarget.getValue(0, time);

	Vec3D u(0,1,0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(p.x, p.y, p.z, t.x, t.y, t.z, u.x, u.y, u.z);
	//float roll = rot.getValue(0, time) / PI * 180.0f;
	//glRotatef(roll, 0, 0, 1);
}


