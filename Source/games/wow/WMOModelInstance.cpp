#include "WMOModelInstance.h"

#include "Game.h"
#include "GameFile.h"
#include "ModelManager.h"
#include "WoWModel.h"

#include "GL/glew.h"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"

void WMOModelInstance::init(char *fname, GameFile &f)
{
  filename = QString::fromLatin1(fname);
  filename = filename.toLower();
  filename.replace(".mdx", ".m2");
  filename.replace(".mdl", ".m2");

  model = 0;

  float ff[3];
  f.read(ff, 12); // Position (X,Z,-Y)
  pos = glm::vec3(ff[0], ff[1], ff[2]);
  f.read(&w, 4); // W component of the orientation quaternion
  f.read(ff, 12); // X, Y, Z components of the orientaton quaternion
  dir = glm::vec3(ff[0], ff[1], ff[2]);
  f.read(&sc, 4); // Scale factor
  f.read(&d1, 4); // (B,G,R,A) Lightning-color. 
  lcol = glm::vec3(((d1 & 0xff0000) >> 16) / 255.0f, ((d1 & 0x00ff00) >> 8) / 255.0f, (d1 & 0x0000ff) / 255.0f);
}

void glQuaternionRotate(const glm::vec3& vdir, float w)
{
  glm::fquat q(w, vdir);
  glm::mat4 m = glm::inverse(glm::toMat4(q));
  glMultMatrixf(glm::value_ptr(m));
}

void WMOModelInstance::draw()
{
  if (!model) return;

  glPushMatrix();

  glTranslatef(pos.x, pos.y, pos.z);
  glm::vec3 vdir(-dir.z, dir.x, dir.y);
  glQuaternionRotate(vdir, w);
  glScalef(sc, -sc, -sc);

  model->draw();
  glPopMatrix();
}

void WMOModelInstance::loadModel(ModelManager &mm)
{
  model = (WoWModel*)mm.items[mm.add(GAMEDIRECTORY.getFile(filename))];
  model->isWMO = true;
}

void WMOModelInstance::unloadModel(ModelManager &mm)
{
  mm.delbyname(filename);
  model = 0;
}