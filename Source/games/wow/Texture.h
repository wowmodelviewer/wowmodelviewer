#ifndef TEXTURE_H
#define TEXTURE_H

#include <cstdint>
#include <vector>

#include "manager.h"
#include "TextureSource.h" // TextureID

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _TEXTURE_API_ __declspec(dllexport)
#    else
#        define _TEXTURE_API_ __declspec(dllimport)
#    endif
#else
#    define _TEXTURE_API_
#endif

// Pixel orders getPixels can return. The values are the OpenGL enums they replace, so stored settings and
// call sites that passed those numbers keep their meaning.
enum TexturePixelOrder : unsigned int
{
  TEXTURE_RGBA = 0x1908,
  TEXTURE_BGRA = 0x80E1
};

// A decoded texture: an RGBA8 mip chain in memory. Nothing here talks to a graphics API; the viewport
// uploads what it needs, and re-uploads when 'generation' changes (a character composite, for instance,
// is drawn again into the same id).
class _TEXTURE_API_ Texture : public ManagedItem
{
public:
  int w, h;
  TextureID id;
  bool compressed;    // the source was block-compressed (DXT); kept for the texture info panels
  unsigned int sourceFormat; // what it was encoded as, by its OpenGL enum (DXT1/3/5, or 0x8058 RGBA8)
  GameFile * file;

  std::vector<std::vector<unsigned char>> mips; // RGBA8, level 0 first
  uint32_t generation;

  Texture(GameFile *);
  explicit Texture(const QString & name);

  // Level 0, w*h*4 bytes, in the order asked for.
  void getPixels(unsigned char * buff, unsigned int format = TEXTURE_RGBA) const;
  void load();

  // Replace the contents with one RGBA or BGRA level (a composite made on the CPU).
  void setPixels(int width, int height, const unsigned char * pixels, unsigned int format);
};

#endif
