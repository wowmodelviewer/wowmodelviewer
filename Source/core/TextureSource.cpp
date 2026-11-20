#include "TextureSource.h"

namespace
{
  core::TexturePixelReader g_reader = nullptr;
}

void core::setTexturePixelReader(TexturePixelReader reader)
{
  g_reader = reader;
}

bool core::readTexturePixels(TextureID id, int & width, int & height, std::vector<unsigned char> & bgra)
{
  width = height = 0;
  bgra.clear();
  return g_reader && g_reader(id, width, height, bgra);
}
