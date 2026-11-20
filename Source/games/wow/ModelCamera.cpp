/*
 * ModelCamera.cpp
 *
 *  Created on: 20 oct. 2013
 *
 */

#include "ModelCamera.h"
#include "RenderList.h"

#include "glm/gtc/matrix_transform.hpp"

#include "database.h"
#include "video.h" // video global var
#include "logger/Logger.h"

void ModelCamera::init(GameFile * f, ModelCameraDef &mcd, std::vector<uint32> & global, std::string modelname)
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

void ModelCamera::initv10(GameFile * f, ModelCameraDefV10 &mcd, std::vector<uint32> & global, std::string modelname)
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

void ModelCamera::setup(float aspect, size_t time)
{
  wmv::RenderList * list = wmv::RenderList::current();
  if (!ok || !list) return;

  list->projection = glm::perspective(glm::radians(fov * 34.5f), aspect, nearclip, farclip * 5);

  const glm::vec3 p = pos + tPos.getValue(0, time);
  const glm::vec3 t = target + tTarget.getValue(0, time);
  list->view = glm::lookAt(p, t, glm::vec3(0, 1, 0));
}


