/*
 * TextureAnim.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "TextureAnim.h"

#include "glm/gtc/matrix_transform.hpp"


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

glm::mat4 TextureAnim::matrix(ssize_t anim) const
{
  glm::mat4 m(1.0f);
  if (trans.uses(anim))
    m = glm::translate(m, tval);
  if (rot.uses(anim))
    m = glm::rotate(m, glm::radians(rval.x), glm::vec3(0, 0, 1)); // as the GL path did: rval.x degrees about z
  if (scale.uses(anim))
    m = glm::scale(m, sval);
  return m;
}

void TextureAnim::init(GameFile * f, ModelTexAnimDef &mta, std::vector<uint32> & global)
{
  trans.init(mta.trans, f, global);
  rot.init(mta.rot, f, global);
  scale.init(mta.scale, f, global);
}


