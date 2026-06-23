/*
 * CharTexture.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "CharTexture.h"


#include <list>
#include <map>
#include <string>

#include <QPainter>

#include "Game.h"
#include "GameFile.h"
#include "Texture.h"
#include "TextureManager.h"
#include "WoWDatabase.h"

#include "logger/Logger.h"

std::map<int, std::pair<LayoutSize, std::map<int,CharRegionCoords> > > CharTexture::LAYOUTS;
constexpr int LAYOUT_BASE_REGION = -1;

void CharTexture::addLayer(GameFile * file, int region, int layer, int blendMode)
{
  if (!file)
    return;

  const CharTextureComponent ct = { file, region, layer, blendMode };
  m_components.push_back(ct);
}

void CharTexture::reset(unsigned int _layoutSizeId)
{
  m_components.clear();
  layoutSizeId = _layoutSizeId;
}

#define DEBUG_TEXTURE 0 // 1 output component names, 2 save intermediate images on disk

void CharTexture::compose(GLuint texID)
{
  if (m_components.empty())
    return;

  std::sort(m_components.begin(), m_components.end());

  QImage img;

#if DEBUG_TEXTURE > 1
  static auto baseidx = 0;
  auto cmpidx = 0;
#endif

  for (auto it : m_components)
  {
    burnComponent(img, it);
#if DEBUG_TEXTURE > 1
    img.save(QString("./ComposedTexture%1_%2.png").arg(baseidx).arg(cmpidx++));
#endif
  }

  // good, upload this to video
  glBindTexture(GL_TEXTURE_2D, texID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, img.bits());
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}

GLuint CharTexture::composeStackToTexture(const std::vector<CharTextureComponent> & layersIn)
{
  std::vector<CharTextureComponent> layers = layersIn;
  std::sort(layers.begin(), layers.end()); // by layer (lowest = base)

  QImage composite;
  for (const auto & c : layers)
  {
    if (!c.file)
      continue;
    QImage * img = gameFileToQImage(c.file);
    if (!img)
      continue;

    if (composite.isNull())
    {
      composite = img->copy(); // first (lowest) layer is the base
    }
    else
    {
      const QImage scaled = (img->size() == composite.size())
        ? *img
        : img->scaled(composite.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

      QPainter::CompositionMode mode = QPainter::CompositionMode_SourceOver;
      switch (c.blendMode)
      {
      case 4: mode = QPainter::CompositionMode_Multiply; break;
      case 6: mode = QPainter::CompositionMode_Overlay; break;
      default: break; // 1 (blit) / 9 (alpha) etc. -> straight alpha over
      }

      QPainter painter(&composite);
      painter.setCompositionMode(mode);
      painter.drawImage(0, 0, scaled);
      painter.end();
    }
    delete img;
  }

  if (composite.isNull())
    return 0;

  GLuint id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, composite.width(), composite.height(), 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, composite.bits());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return id;
}

void CharTexture::initRegions()
{
  auto layouts = GAMEDATABASE.sqlQuery("SELECT ID, Width, Height FROM CharComponentTextureLayouts");

  if(!layouts.valid || layouts.empty())
  {
    LOG_ERROR << "Fail to retrieve Texture Layout information from game database";
    return;
  }

  // Iterate on layout to initialize our members (sections informations)
  for (auto& value : layouts.values)
  {
    LayoutSize texLayout = { value[1].toInt() , value[2].toInt() };
    auto curLayout = value[0].toInt();

    // search all regions for this layout
    auto regions = GAMEDATABASE.sqlQuery(QString("SELECT SectionType, X, Y, Width, Height  FROM CharComponentTextureSections WHERE CharComponentTextureLayoutID = %1").arg(curLayout));

    if(!regions.valid || regions.empty())
    {
      LOG_ERROR << "Fail to retrieve Section Layout information from game database for layout" << curLayout;
      continue;
    }

    std::map<int,CharRegionCoords> regionCoords;
    const CharRegionCoords base = { 0, 0, texLayout.width, texLayout.height };
    regionCoords[LAYOUT_BASE_REGION] = base;

    for (auto& r : regions.values)
    {
      const CharRegionCoords coords = { r[1].toInt(), r[2].toInt(), r[3].toInt(), r[4].toInt() };
      regionCoords[r[0].toInt()] = coords;
    }
    LOG_INFO << "Found" << regionCoords.size() << "regions for layout" << curLayout;
    CharTexture::LAYOUTS[curLayout] = make_pair(texLayout,regionCoords);
  }

}

void CharTexture::burnComponent(QImage & destImage, CharTextureComponent & ct) const
{
  auto layoutInfos = CharTexture::LAYOUTS[layoutSizeId];

  const auto &coords = layoutInfos.second[ct.region];

  auto * tmp = gameFileToQImage(ct.file);

  if (!tmp)
    return;

  const auto newImage = tmp->scaled(coords.width, coords.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);


#if DEBUG_TEXTURE > 0
  const auto region = ct.region;
  const auto layer = ct.layer;
  const auto x = coords.xpos;
  const auto y = coords.ypos;
  const auto width = coords.width;
  const auto height = coords.height;
  LOG_INFO << __FUNCTION__ << ct.file->fullname() << region << layer << x << y << width << height;
#endif

#if DEBUG_TEXTURE > 1
  newImage.save(QString("./tex__%1_%2_%3_%4_%5.png").arg(region).arg(x).arg(y).arg(width).arg(height));
#endif

  if ((ct.region == LAYOUT_BASE_REGION 
       || ct.region == 11) // Dracthyr dragon base section
      && ct.layer == 0)
  {
    destImage = newImage;
  }
  else
  {
    const auto destPos = QPoint(coords.xpos, coords.ypos);
    QPainter painter(&destImage);
    
    QPainter::CompositionMode mode = QPainter::CompositionMode_SourceOver;

    switch(ct.blendMode)
    {
    case 4:
      mode = QPainter::CompositionMode_Multiply;
      break;
    case 6:
      mode = QPainter::CompositionMode_Overlay;
      break;
    default:
      break;
    }
    
    painter.setCompositionMode(mode);

    painter.drawImage(destPos, newImage);
    painter.end();
  }

  delete tmp;
}

void imageCleanUpHandler(void * ptr)
{
  free(ptr);
}

QImage * CharTexture::gameFileToQImage(GameFile * file)
{
  // A character composition decodes the same source BLP many times: each layer is
  // reused across regions and re-composed on every customization change (Dracthyr
  // measured 337 decodes for 40 distinct files -- the base skin alone 29x). Decoding
  // a BLP (CASC read + decompress + GL round-trip via getPixels) is the dominant cost
  // of a character load, and TextureManager can't help because we del() right after.
  // Cache the decoded image per source file instead. Keying by name is safe: a name
  // resolves to fixed content, and different customization choices are different files.
  // Bounded with simple LRU eviction so memory can't grow without limit.
  static std::map<QString, QImage> s_cache;
  static std::list<QString> s_lru;
  const size_t CACHE_MAX = 64;

  const QString name = file->fullname();

  auto cached = s_cache.find(name);
  if (cached != s_cache.end())
  {
    s_lru.remove(name);
    s_lru.push_back(name);
    return new QImage(cached->second); // implicitly-shared copy, no decode
  }

  const auto temptex = TEXTUREMANAGER.add(file);
  auto * tex = dynamic_cast<Texture*>(TEXTUREMANAGER.items[temptex]);

  // Alfred 2009.07.03, tex width or height can't be zero
  if (tex->w == 0 || tex->h == 0)
  {
    TEXTUREMANAGER.del(temptex);
    return nullptr;
  }

  auto * tempbuf = (unsigned char*)malloc(tex->w*tex->h * 4);

  tex->getPixels(tempbuf, GL_BGRA_EXT);
  QImage decoded(tempbuf, tex->w, tex->h, QImage::Format_ARGB32, imageCleanUpHandler, tempbuf);

  TEXTUREMANAGER.del(temptex);

  // Store an independent deep copy; hand the caller its own shared copy.
  QImage entry = decoded.copy();
  if (s_cache.size() >= CACHE_MAX && !s_lru.empty())
  {
    s_cache.erase(s_lru.front());
    s_lru.pop_front();
  }
  s_cache[name] = entry;
  s_lru.push_back(name);

  return new QImage(entry);
}

