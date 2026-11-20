/*
 * ModelLight.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "ModelLight.h"

#include <glm/gtc/type_ptr.hpp>


#include "wow_enums.h"
#include "logger/Logger.h"


void ModelLight::init(GameFile * f, ModelLightDef &mld, std::vector<uint32> & global)
{
  tpos = pos = mld.pos;
  tdir = dir = glm::vec3(0,1,0); // no idea
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

void ModelLight::evaluate(size_t time, glm::vec4 & p, glm::vec4 & diffcol, glm::vec4 & ambcol)
{
  ambcol = glm::vec4(ambColor.getValue(0, time) * ambIntensity.getValue(0, time), 1.0f);
  diffcol = glm::vec4(diffColor.getValue(0, time) * diffIntensity.getValue(0, time), 1.0f);
  if (type == MODELLIGHT_DIRECTIONAL)
    p = glm::vec4(tdir, 0.0f);
  else
  {
    p = glm::vec4(tpos, 1.0f);
    if (type != MODELLIGHT_POINT)
      LOG_ERROR << "Light type" << type << "is unknown.";
  }
}
