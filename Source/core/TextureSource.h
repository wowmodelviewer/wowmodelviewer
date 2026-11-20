/*
 * TextureSource.h
 *
 * The graphics-free way for core code (exporters) to read a game texture's pixels. Texture ids are plain
 * handles owned by the wow library's TextureManager, which registers itself here at load time; nothing in
 * core or the plugins talks to a graphics API.
 */

#ifndef _TEXTURESOURCE_H_
#define _TEXTURESOURCE_H_

#include <cstdint>
#include <vector>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _TEXTURESOURCE_API_ __declspec(dllexport)
#    else
#        define _TEXTURESOURCE_API_ __declspec(dllimport)
#    endif
#else
#    define _TEXTURESOURCE_API_
#endif

typedef uint32_t TextureID;

namespace core
{
  // Fills width, height and the level-0 pixels in BGRA byte order. Returns false for an unknown id.
  typedef bool (*TexturePixelReader)(TextureID id, int & width, int & height, std::vector<unsigned char> & bgra);

  _TEXTURESOURCE_API_ void setTexturePixelReader(TexturePixelReader reader);
  _TEXTURESOURCE_API_ bool readTexturePixels(TextureID id, int & width, int & height, std::vector<unsigned char> & bgra);
}

#endif
