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
  int blendMode;

  bool operator<(const CharTextureComponent& c) const
  {
  return layer < c.layer;
  }
};

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _CHARTEXTURE_API_ __declspec(dllexport)
#    else
#        define _CHARTEXTURE_API_ __declspec(dllimport)
#    endif
#else
#    define _CHARTEXTURE_API_
#endif

class _CHARTEXTURE_API_ CharTexture
{
  public:
    explicit CharTexture(unsigned int _layoutSizeId = 0)
      : layoutSizeId(_layoutSizeId)
    {}

    void addLayer(GameFile * file, int region, int layer, int blendMode = 1);
    void addComponent(CharTextureComponent c) { m_components.push_back(c); }

    void compose(GLuint texID);

    void reset(unsigned int _layoutSizeId);

    static void initRegions();

    // Decode + layer a stack of full-image textures into ONE new GL texture, returning its id
    // (0 on failure). Used for a slot the model samples as a single texture but that
    // customization splits across multiple layers -- e.g. the eye (TextureType 19), where the
    // coloured iris and the Eyesight glow are separate textures that must be composited rather
    // than overwrite each other in one replaceTextures slot. Layers are applied in 'layer'
    // order (lowest first = base); 'region' is ignored (each layer fills the whole image).
    // The caller OWNS the returned texture and must glDeleteTextures() it.
    static GLuint composeStackToTexture(const std::vector<CharTextureComponent> & layers);

  private:
    void burnComponent(QImage & destImage, CharTextureComponent &) const;
    static QImage * gameFileToQImage(GameFile * file);
    unsigned int layoutSizeId;
    std::vector<CharTextureComponent> m_components;
    static std::map<int, std::pair<LayoutSize, std::map<int,CharRegionCoords> > > LAYOUTS;
};


#endif /* _CHARTEXTURE_H_ */
