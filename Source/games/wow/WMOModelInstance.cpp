#include "WMOModelInstance.h"

#include "Game.h"
#include "GameFile.h"
#include "ModelManager.h"
#include "WoWModel.h"

#include "RenderList.h"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"

// Read the 36-byte per-instance transform (position / quaternion / scale / colour) shared
// by the classic (by-name) and modern (by-FileDataID) doodad definitions.
static void readDoodadTransform(WMOModelInstance & mi, GameFile & f)
{
  float ff[3];
  f.read(ff, 12); // Position (X,Z,-Y)
  mi.pos = glm::vec3(ff[0], ff[1], ff[2]);
  f.read(&mi.w, 4); // W component of the orientation quaternion
  f.read(ff, 12); // X, Y, Z components of the orientaton quaternion
  mi.dir = glm::vec3(ff[0], ff[1], ff[2]);
  f.read(&mi.sc, 4); // Scale factor
  f.read(&mi.d1, 4); // (B,G,R,A) Lightning-color.
  mi.lcol = glm::vec3(((mi.d1 & 0xff0000) >> 16) / 255.0f, ((mi.d1 & 0x00ff00) >> 8) / 255.0f, (mi.d1 & 0x0000ff) / 255.0f);
}

void WMOModelInstance::init(char *fname, GameFile &f)
{
  fileDataId = 0;
  filename = QString::fromLatin1(fname);
  filename = filename.toLower();
  filename.replace(".mdx", ".m2");
  filename.replace(".mdl", ".m2");

  model = 0;

  readDoodadTransform(*this, f);
}

void WMOModelInstance::init(int fileDataID, GameFile &f)
{
  // Modern doodad: the model is referenced by FileDataID; its name (if any) is resolved
  // lazily in loadModel(). A fileDataID of 0 means "no model" -- we still read the
  // transform so the surrounding MODD loop stays aligned to the next record.
  fileDataId = fileDataID;
  filename = "";
  model = 0;

  readDoodadTransform(*this, f);
}

void WMOModelInstance::draw()
{
  wmv::RenderList * list = wmv::RenderList::current();
  if (!model || !list) return;

  list->push();
  list->translate(pos);
  const glm::vec3 vdir(-dir.z, dir.x, dir.y);
  list->multiply(glm::inverse(glm::toMat4(glm::fquat(w, vdir))));
  list->scale(glm::vec3(sc, -sc, -sc));

  model->draw();
  list->pop();
}

void WMOModelInstance::loadModel(ModelManager &mm)
{
  // Modern doodads are addressed by FileDataID; classic ones by name.
  GameFile * gf = (fileDataId > 0) ? GAMEDIRECTORY.getFile(fileDataId)
                                   : GAMEDIRECTORY.getFile(filename);
  if (!gf)
    return; // missing / id 0 (e.g. an empty modern doodad slot)

  if (filename.isEmpty())
    filename = gf->fullname(); // so unloadModel(delbyname) can find it again

  model = (WoWModel*)mm.items[mm.add(gf)];
  if (model)
    model->isWMO = true;
}

void WMOModelInstance::unloadModel(ModelManager &mm)
{
  mm.delbyname(filename);
  model = 0;
}