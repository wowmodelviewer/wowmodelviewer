#ifndef _WMO_MODELINSTANCE_H_
#define _WMO_MODELINSTANCE_H_

#include "vec3d.h"

#include <QString>

class GameFile;
class ModelManager;
class WoWModel;

class WMOModelInstance {
  public:
  // header
  Vec3D pos;		// Position
  float w;		// W for Quat Rotation
  Vec3D dir;		// Direction for Quat Rotation
  float sc;		// Scale Factor
  unsigned int d1;

  WoWModel *model;
  QString filename;
  int id;
  unsigned int scale;
  int light;
  Vec3D ldir;
  Vec3D lcol;

  WMOModelInstance() {}
  void init(char *fname, GameFile &f);
  void draw();

  void loadModel(ModelManager &mm);
  void unloadModel(ModelManager &mm);
};

#endif
