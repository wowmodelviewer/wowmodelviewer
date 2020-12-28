/*
 * Bone.cpp
 *
 *  Created on: 19 oct. 2013
 *
 */

#include "Bone.h"

#include "GL/glew.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>


#include "logger/Logger.h"

void Bone::calcMatrix(std::vector<Bone> & allbones, ssize_t anim, size_t time, bool rotate)
{
  if (calc)
    return;

  glm::mat4 m(1.0f);
  glm::fquat q;

  bool tr = rot.uses(anim) || scale.uses(anim) || trans.uses(anim) || billboard;
  if (tr) 
  {
    m = glm::translate(m, pivot);

    if (trans.uses(anim))
      m = glm::translate(m, trans.getValue(anim, time));

    if (rot.uses(anim) && rotate)
      m = m * glm::toMat4(rot.getValue(anim, time)); 

    if (scale.uses(anim))
      m = glm::scale(m, scale.getValue(anim, time));

    if (billboard)
    {
      float modelview[16];
      glGetFloatv(GL_MODELVIEW_MATRIX, modelview);

      glm::vec3 vRight = glm::vec3(modelview[0], modelview[4], modelview[8]);
      glm::vec3 vUp = glm::vec3(modelview[1], modelview[5], modelview[9]); // Spherical billboarding
      //glm::vec3 vUp = glm::vec3(0,1,0); // Cylindrical billboarding
      vRight = vRight * -1.f;
      m[0][2] = vRight.x;
      m[1][2] = vRight.y;
      m[2][2] = vRight.z;
      m[0][1] = vUp.x;
      m[1][1] = vUp.y;
      m[2][1] = vUp.z;
    }
    
    m = glm::translate(m, pivot * -1.0f);

  }

  if (parent > -1) 
  {
    allbones[parent].calcMatrix(allbones, anim, time, rotate);
    mat = allbones[parent].mat * m;
  }
  else
  {
    mat = m;
  }

  // transform matrix for normal vectors ... ??
  if (rot.uses(anim) && rotate) 
  {
    if (parent>=0)
      mrot = allbones[parent].mrot * glm::toMat4(q);
    else
      mrot = glm::toMat4(q);
  }
  else
  {
    mrot = glm::mat4(1.0f);
  }

  transPivot = glm::vec3(mat * glm::vec4(pivot, 1.0f));

  calc = true;
}

void Bone::initV3(GameFile & f, ModelBoneDef &b, const modelAnimData & data)
{
  calc = false;

  parent = b.parent;
  pivot = b.pivot;
  billboard = (b.flags & MODELBONE_BILLBOARD) != 0;

  boneDef = b;

  trans.init(b.translation, f, data);
  rot.init(b.rotation, f, data);
  scale.init(b.scaling, f, data);
}

