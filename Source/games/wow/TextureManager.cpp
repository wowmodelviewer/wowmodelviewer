
#define _TEXTUREMANAGER_CPP_
#include "TextureManager.h"
#undef _TEXTUREMANAGER_CPP_

#include "logger/Logger.h"

#include "GameFile.h"
#include "Texture.h"

_TEXTUREMANAGER_API_ TextureManager TEXTUREMANAGER;

namespace
{
  bool readPixels(TextureID id, int & width, int & height, std::vector<unsigned char> & bgra)
  {
    const Texture * tex = TEXTUREMANAGER.texture(id);
    if (!tex || tex->mips.empty() || tex->w <= 0 || tex->h <= 0)
      return false;
    width = tex->w;
    height = tex->h;
    bgra.resize((size_t)width * height * 4);
    tex->getPixels(bgra.data(), TEXTURE_BGRA);
    return true;
  }
}

TextureManager::TextureManager()
: m_nextId(1)
{
  core::setTexturePixelReader(&readPixels);
}

TextureID TextureManager::add(GameFile * file)
{
  if (!file)
    return 0;

  const QString name = file->fullname();

  // if the item already exists, return the existing ID
  auto found = names.find(name);
  if (found != names.end())
  {
    items[found->second]->addref();
    return found->second;
  }

  Texture * tex = new Texture(file);
  tex->id = takeId();
  tex->load();
  do_add(name, tex->id, tex);
  return tex->id;
}

TextureID TextureManager::create(const QString & name)
{
  const TextureID id = takeId();
  // a unique key, so two composites with the same label never share an id (and del finds it by its item name)
  const QString key = QString("%1#%2").arg(name).arg(id);
  Texture * tex = new Texture(key);
  tex->id = id;
  do_add(key, tex->id, tex);
  return tex->id;
}

bool TextureManager::setPixels(TextureID id, int width, int height, const unsigned char * pixels, unsigned int format)
{
  Texture * tex = texture(id);
  if (!tex)
    return false;
  tex->setPixels(width, height, pixels, format);
  return true;
}

TextureID TextureManager::takeId()
{
  if (m_nextId == 50000)
    m_nextId++; // ModelRenderPass::INVALID_TEX, a sentinel the model code compares texture ids with
  return m_nextId++;
}

Texture * TextureManager::texture(TextureID id) const
{
  auto it = items.find(id);
  return it == items.end() ? nullptr : static_cast<Texture *>(it->second);
}

void TextureManager::doDelete(TextureID)
{
  // The pixels go with the Texture item, which Manager::del deletes; the viewport drops its GPU copy when it
  // no longer sees the id.
}
