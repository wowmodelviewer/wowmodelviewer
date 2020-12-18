#ifndef _WMO_LIGHT_H_
#define _WMO_LIGHT_H_

#include "quaternion.h" // Vec4D
#include "glm/glm.hpp"

typedef int GLint;

class GameFile;

struct WMOLight {
  unsigned int lighttype, type, useatten, color;
  glm::vec3 pos;
  float intensity;
  float attenStart, attenEnd;
  float unk[3];
  float r;

  Vec4D fcolor;

  void init(GameFile &f);
  void setup(GLint light);

  static void setupOnce(GLint light, glm::vec3 dir, glm::vec3 lcol);
};

#endif
