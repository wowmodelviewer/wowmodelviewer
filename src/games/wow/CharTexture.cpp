/*
 * CharTexture.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "CharTexture.h"


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

void CharTexture::addLayer(GameFile * file, int region, int layer)
{
  if (!file)
    return;

  const CharTextureComponent ct = { file, region, layer };
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
    auto regions = GAMEDATABASE.sqlQuery(QString("SELECT Section, X, Y, Width, Height  FROM CharComponentTextureSections WHERE LayoutID = %1").arg(curLayout));

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
      //LOG_INFO << regions.values[r][0].toInt()+1 << " " << coords.xpos << " " << coords.ypos << " " << coords.width << " " << coords.height << std::endl;
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
  const auto x = coords.xpos;
  const auto y = coords.ypos;
  const auto width = coords.width;
  const auto height = coords.height;
  LOG_INFO << __FUNCTION__ << ct.file->fullname() << region << x << y << width << height;
#endif

#if DEBUG_TEXTURE > 1
  newImage.save(QString("./tex__%1_%2_%3_%4_%5.png").arg(region, x, y, width, height));
#endif

  if (ct.region == LAYOUT_BASE_REGION)
  {
    destImage = newImage;
  }
  else
  {
    const auto destPos = QPoint(coords.xpos, coords.ypos);
    QPainter painter(&destImage);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
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
  QImage * result = nullptr;
  const auto temptex = TEXTUREMANAGER.add(file);
  auto * tex = dynamic_cast<Texture*>(TEXTUREMANAGER.items[temptex]);

  // Alfred 2009.07.03, tex width or height can't be zero
  if (tex->w == 0 || tex->h == 0)
  {
    TEXTUREMANAGER.del(temptex);
    return result;
  }

  auto * tempbuf = (unsigned char*)malloc(tex->w*tex->h * 4);

  tex->getPixels(tempbuf, GL_BGRA_EXT);
  result = new QImage(tempbuf, tex->w, tex->h, QImage::Format_ARGB32, imageCleanUpHandler, tempbuf);
  
  TEXTUREMANAGER.del(temptex);

  return result;
}

