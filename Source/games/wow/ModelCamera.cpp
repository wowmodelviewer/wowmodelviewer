#include "ModelCamera.h"
#include "database.h"
#include "video.h" // video global var
#include "logger/Logger.h"
#include "GL/glew.h"

void ModelCamera::init(GameFile* f, ModelCameraDef& mcd, std::vector<uint32>& global, std::string modelname)
{
	LOG_INFO << "Using original Camera Model Definitions.";
	ok = true;
	nearclip = mcd.nearclip;
	farclip = mcd.farclip;
	fov = mcd.fov;
	pos = mcd.pos;
	target = mcd.target;
	tPos.init(mcd.transPos, f, global);
	tTarget.init(mcd.transTarget, f, global);
	rot.init(mcd.rot, f, global);
}

void ModelCamera::initv10(GameFile* f, ModelCameraDefV10& mcd, std::vector<uint32>& global, std::string modelname)
{
	LOG_INFO << "Using version 10 Camera Model Definitions.";
	ok = true;
	nearclip = mcd.nearclip;
	farclip = mcd.farclip;
	pos = mcd.pos;
	target = mcd.target;
	tPos.init(mcd.transPos, f, global);
	tTarget.init(mcd.transTarget, f, global);
	rot.init(mcd.rot, f, global);
	fov = 0.95f;
}

void ModelCamera::setup(size_t time)
{
	if (!ok) return;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(fov * 34.5f, static_cast<GLfloat>(video.xRes) / static_cast<GLfloat>(video.yRes), nearclip, farclip * 5);

	const glm::vec3 p = pos + tPos.getValue(0, time);
	const glm::vec3 t = target + tTarget.getValue(0, time);

	const glm::vec3 u(0, 1, 0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(p.x, p.y, p.z, t.x, t.y, t.z, u.x, u.y, u.z);
	//float roll = rot.getValue(0, time) / PI * 180.0f;
	//glRotatef(roll, 0, 0, 1);
}
