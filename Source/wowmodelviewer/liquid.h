#ifndef LIQUID_H
#define LIQUID_H


#include <wx/string.h>

#include "maptile.h" // CHUNKSIZE

#include "glm/glm.hpp"


class GameFile;

const float LQ_DEFAULT_TILESIZE = CHUNKSIZE / 8.0f;

// handle liquids like oceans, lakes, rivers, slime, magma
class Liquid {

  int xtiles, ytiles;
  GLuint dlist;

  glm::vec3 pos;

  float tilesize;
  float ydir;
  float texRepeats;

  void initGeometry(GameFile &f);
  void initTextures(wxString basename, int first, int last);

  int type;
  
  glm::vec3 col;
  int tmpflag;
  bool trans;

  int shader;

public:

  std::vector<GLuint> textures;

  Liquid(int x, int y, glm::vec3 base, float tilesize = LQ_DEFAULT_TILESIZE):
    xtiles(x), ytiles(y), pos(base), tilesize(tilesize), ydir(1.0f), shader(-1)
  {
  }
  ~Liquid();

  //void init(GameFile &f);
  void initFromTerrain(GameFile &f, int flags);
  void initFromWMO(GameFile &f, WMOMaterial &mat, bool indoor);

  void draw();


};



#endif
