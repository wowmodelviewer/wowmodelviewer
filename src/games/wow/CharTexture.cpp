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
#include "GameDatabase.h"
#include "GameFile.h"
#include "Texture.h"
#include "types.h"

#include "logger/Logger.h"

std::map<int, std::pair<LayoutSize, std::map<int,CharRegionCoords> > > CharTexture::LAYOUTS;

void CharTexture::addLayer(GameFile * file, int region, int layer)
{
  //std::cout << __FUNCTION__ << " " << fn.mb_str() << " " << region << " " << layer << std::endl;
  if (!file)
    return;

  CharTextureComponent ct;
  ct.file = file;
  ct.region = region;
  ct.layer = layer;
  m_components.push_back(ct);
}

void CharTexture::reset(unsigned int _layoutSizeId)
{
  m_components.clear();
  layoutSizeId = _layoutSizeId;
}

#define DEBUG_TEXTURE 0

void CharTexture::compose(TextureID texID)
{
  std::pair<LayoutSize, std::map<int, CharRegionCoords> > layoutInfos = CharTexture::LAYOUTS[layoutSizeId];

	// if we only have one texture then don't bother with compositing
	if (m_components.size()==1)
	{
		Texture temp(m_components[0].file);
    temp.id = texID;
    temp.load();
		return;
	}

	std::sort(m_components.begin(), m_components.end());

  // burn base image
  QImage * img = gameFileToQImage(baseImage);

#if DEBUG_TEXTURE > 0
	static int tmpidx = 0;
#endif
  for (auto it : m_components)
  {
    burnComponent(*img, it);
#if DEBUG_TEXTURE > 1
    img->save(QString("./ComposedTexture%1.png").arg(tmpidx++));
#endif
  }

	// good, upload this to video
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, layoutInfos.first.width, layoutInfos.first.height, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, img->bits());
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

  delete img;
}

void CharTexture::initRegions()
{
  sqlResult layouts = GAMEDATABASE.sqlQuery("SELECT ID, Width, Height FROM CharComponentTextureLayouts");

  if(!layouts.valid || layouts.empty())
  {
    LOG_ERROR << "Fail to retrieve Texture Layout information from game database";
    return;
  }

  // Iterate on layout to initialize our members (sections informations)
  for(int i=0, imax=layouts.values.size() ; i < imax ; i++)
  {
    LayoutSize texLayout;
    int curLayout = layouts.values[i][0].toInt();
    texLayout.width = layouts.values[i][1].toInt();
    texLayout.height = layouts.values[i][2].toInt();

    // search all regions for this layout
    QString query = QString("SELECT Section, X, Y, Width, Height  FROM CharComponentTextureSections WHERE LayoutID = %1").arg(curLayout);
    sqlResult regions = GAMEDATABASE.sqlQuery(query);

    if(!regions.valid || regions.empty())
    {
      LOG_ERROR << "Fail to retrieve Section Layout information from game database for layout" << curLayout;
      continue;
    }

    std::map<int,CharRegionCoords> regionCoords;
    CharRegionCoords base;
    base.xpos = 0;
    base.ypos = 0;
    base.width = texLayout.width;
    base.height = texLayout.height;
    regionCoords[0] = base;

    for(int r=0, rmax=regions.values.size() ; r < rmax ; r++)
    {
      CharRegionCoords coords;
      coords.xpos = regions.values[r][1].toInt();
      coords.ypos = regions.values[r][2].toInt();
      coords.width = regions.values[r][3].toInt();
      coords.height = regions.values[r][4].toInt();
      //LOG_INFO << regions.values[r][0].toInt()+1 << " " << coords.xpos << " " << coords.ypos << " " << coords.width << " " << coords.height << std::endl;
      regionCoords[regions.values[r][0].toInt()] = coords;
    }
    LOG_INFO << "Found" << regionCoords.size() << "regions for layout" << curLayout;
    CharTexture::LAYOUTS[curLayout] = make_pair(texLayout,regionCoords);
  }

}

void CharTexture::burnComponent(QImage & destImage, CharTextureComponent & ct)
{
  std::pair<LayoutSize, std::map<int, CharRegionCoords> > layoutInfos = CharTexture::LAYOUTS[layoutSizeId];

  const CharRegionCoords &coords = layoutInfos.second[ct.region];

  QImage * tmp = gameFileToQImage(ct.file);

  if (!tmp)
    return;

  QImage newImage = tmp->scaled(coords.width, coords.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);


#if DEBUG_TEXTURE > 1
  int region = ct.region;
  int x = coords.xpos;
  int y = coords.ypos;
  int width = coords.width;
  int height = coords.height;
  LOG_INFO << __FUNCTION__ << region << x << y << width << height;
  newImage.save(QString("./tex__%1_%2_%3_%4_%5.png").arg(region, x, y, width, height));
#endif

  QPoint destPos = QPoint(coords.xpos, coords.ypos);
  QPainter painter(&destImage);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.drawImage(destPos, newImage);
  painter.end();
}

void imageCleanUpHandler(void * ptr)
{
  free(ptr);
}

QImage * CharTexture::gameFileToQImage(GameFile * file)
{
  QImage * result = 0;
  TextureID temptex = TEXTUREMANAGER.add(file);
  Texture * tex = dynamic_cast<Texture*>(TEXTUREMANAGER.items[temptex]);

  // Alfred 2009.07.03, tex width or height can't be zero
  if (tex->w == 0 || tex->h == 0)
  {
    TEXTUREMANAGER.del(temptex);
    return result;
  }

  unsigned char * tempbuf = (unsigned char*)malloc(tex->w*tex->h * 4);

  tex->getPixels(tempbuf, GL_BGRA_EXT);
  result = new QImage(tempbuf, tex->w, tex->h, QImage::Format_ARGB32, imageCleanUpHandler, tempbuf);
  
  TEXTUREMANAGER.del(temptex);

  return result;
}

