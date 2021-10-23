/*
 * ModelCamera.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "ModelCamera.h"

#include "database.h"
#include "video.h" // video global var
#include "Logger.h"
#include "GL/glew.h"

void ModelCamera::init(GameFile * f, M2Camera &mcd, std::vector<uint32> & global, std::string modelname)
{
  LOG_INFO << "Using original Camera Model Definitions.";
  ok = true;
  nearclip = mcd.near_clip;
  farclip = mcd.far_clip;
  //fov = mcd.FoV; @TODO : to fix
  pos = mcd.position_base;
  target = mcd.target_position_base;
  /*
  @TODO : to fix
  tPos.init(mcd.transPos, f, global);
  tTarget.init(mcd.transTarget, f, global);
  rot.init(mcd.rot, f, global);
  */
}

void ModelCamera::setup(size_t time)
{
  if (!ok) return;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(fov * 34.5f, (GLfloat)video.xRes/(GLfloat)video.yRes, nearclip, farclip*5);

  glm::vec3 p = pos + tPos.getValue(0, time);
  glm::vec3 t = target + tTarget.getValue(0, time);

  glm::vec3 u(0,1,0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(p.x, p.y, p.z, t.x, t.y, t.z, u.x, u.y, u.z);
  //float roll = rot.getValue(0, time) / PI * 180.0f;
  //glRotatef(roll, 0, 0, 1);
}


