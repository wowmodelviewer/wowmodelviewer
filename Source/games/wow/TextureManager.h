
#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include <vector>

#include "manager.h"
#include "TextureSource.h" // TextureID

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _TEXTUREMANAGER_API_ __declspec(dllexport)
#    else
#        define _TEXTUREMANAGER_API_ __declspec(dllimport)
#    endif
#else
#    define _TEXTUREMANAGER_API_
#endif

class GameFile;
class Texture;

// Every texture the viewer holds, decoded in memory and named by a TextureID (0 is "none"). Ids are handed
// out in increasing order and never reused while the process runs, so the viewport can key its GPU copies on
// (id, generation) without ever drawing a stale image.
class _TEXTUREMANAGER_API_ TextureManager : public Manager<TextureID>
{
public:
  TextureManager();

  virtual TextureID add(GameFile *);

  // A texture whose pixels are made by the viewer (a character or eye composite). It starts empty; the caller
  // owns the reference and releases it with del().
  TextureID create(const QString & name);
  // Replace a texture's pixels (TEXTURE_RGBA or TEXTURE_BGRA order); its generation goes up.
  bool setPixels(TextureID id, int width, int height, const unsigned char * pixels, unsigned int format);

  // The texture, or nullptr when the id is unknown.
  Texture * texture(TextureID id) const;

  void doDelete(TextureID id);

private:
  TextureID takeId();
  TextureID m_nextId;
};

_TEXTUREMANAGER_API_ extern TextureManager TEXTUREMANAGER;

#endif
