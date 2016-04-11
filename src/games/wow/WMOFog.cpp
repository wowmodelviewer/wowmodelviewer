#include "WMOFog.h"

#include "GameFile.h"

void WMOFog::init(GameFile &f)
{
  f.read(this, 0x30);
  color = Vec4D(((color1 & 0x00FF0000) >> 16) / 255.0f, ((color1 & 0x0000FF00) >> 8) / 255.0f,
                (color1 & 0x000000FF) / 255.0f, ((color1 & 0xFF000000) >> 24) / 255.0f);
  float temp;
  temp = pos.y;
  pos.y = pos.z;
  pos.z = -temp;
  fogstart = fogstart * fogend;
}

void WMOFog::setup()
{
  /*
  if (gWorld->drawfog) {
  glFogfv(GL_FOG_COLOR, color);
  glFogf(GL_FOG_START, fogstart);
  glFogf(GL_FOG_END, fogend);

  glEnable(GL_FOG);
  } else {
  glDisable(GL_FOG);
  }
  */
}