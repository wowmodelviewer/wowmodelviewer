/*
 * CharTexture.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#ifndef _CHARTEXTURE_H_
#define _CHARTEXTURE_H_

#include <map>

#include "GL/glew.h"

#include <QImage>

class GameFile;

struct CharRegionCoords {
  int xpos, ypos, width, height;
};

struct LayoutSize {
  int width, height;
};

struct CharTextureComponent
{
  GameFile * file;
  int region;
  int layer;

  bool operator<(const CharTextureComponent& c) const
  {
  return layer < c.layer;
  }
};

class CharTexture
{
  public:
    explicit CharTexture(unsigned int _layoutSizeId = 0)
      : layoutSizeId(_layoutSizeId)
    {}

    void addLayer(GameFile * file, int region, int layer);
    void addComponent(CharTextureComponent c) { m_components.push_back(c); }

    void compose(GLuint texID);

    void reset(unsigned int _layoutSizeId);

    static void initRegions();

  private:
    void burnComponent(QImage & destImage, CharTextureComponent &) const;
    static QImage * gameFileToQImage(GameFile * file);
    unsigned int layoutSizeId;
    std::vector<CharTextureComponent> m_components;
    static std::map<int, std::pair<LayoutSize, std::map<int,CharRegionCoords> > > LAYOUTS;
};


#endif /* _CHARTEXTURE_H_ */
