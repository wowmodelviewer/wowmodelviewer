/*
 * CharTexture.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "CharTexture.h"

#include "enums.h" // NUM_REGIONS

#include "GameDatabase.h"

#include "Logger/Logger.h"

#include "CxImage/ximage.h"

#include <QImage>

std::map<int, pair<LayoutSize, std::map<int,CharRegionCoords> > > CharTexture::LAYOUTS;

void CharTexture::addLayer(wxString fn, int region, int layer)
{
  //std::cout << __FUNCTION__ << " " << fn.mb_str() << " " << region << " " << layer << std::endl;
  if (!fn || fn.length()==0)
    return;

  CharTextureComponent ct;
  ct.name = fn;
  ct.region = region;
  ct.layer = layer;
  m_components.push_back(ct);
}

void CharTexture::reset(size_t _layoutSizeId)
{
  m_components.clear();
  layoutSizeId = _layoutSizeId;
}

// 2007.07.03 Alfred, enlarge buf size and make it static to prevent stack overflow
//static unsigned char destbuf[REGION_PX*REGION_PX*4], tempbuf[REGION_PX*REGION_PX*4];
void CharTexture::compose(TextureID texID)
{
  //std::cout << "texID = " << texID << std::endl;

  pair<LayoutSize, std::map<int, CharRegionCoords> > layoutInfos = CharTexture::LAYOUTS[layoutSizeId];

  //std::cout << __FUNCTION__ << " " << __LINE__ << " " << layoutInfos.first.width << "x" << layoutInfos.first.height << std::endl;

	// if we only have one texture then don't bother with compositing
  // std::cout << "nb component = " << m_components.size() << std::endl;
	if (m_components.size()==1)
	{
		Texture temp(m_components[0].name);
		texturemanager.LoadBLP(texID, &temp);
		return;
	}

	std::sort(m_components.begin(), m_components.end());

	unsigned char *destbuf, *tempbuf;

	destbuf = (unsigned char*)calloc(layoutInfos.first.width*layoutInfos.first.height,4);

	for (std::vector<CharTextureComponent>::iterator it = m_components.begin(); it != m_components.end(); ++it)
	{
		CharTextureComponent &comp = *it;
		//std::cout << __FUNCTION__ << " " << comp.name.c_str() << std::endl;
    // pandaren with different regions.
		const CharRegionCoords &coords = layoutInfos.second[comp.region];
		/*
		std::cout << "coords :" << std::endl;
		std::cout << coords.xpos << " " << coords.ypos << " " << coords.width << " " << coords.height << std::endl;
		*/
		TextureID temptex = texturemanager.add(comp.name);
		Texture * tex = dynamic_cast<Texture*>(texturemanager.items[temptex]);


		// Alfred 2009.07.03, tex width or height can't be zero
		if (tex->w == 0 || tex->h == 0)
		{
			texturemanager.del(temptex);
			continue;
		}
		tempbuf = (unsigned char*)calloc(tex->w*tex->h,4);

		//std::cout << "tex->w = " << tex->w << " vs coords.width = " << coords.width << std::endl;
		//std::cout << "tex->h = " << tex->h << " vs coords.height = " << coords.height << std::endl;
		if (tex->w!=coords.width || tex->h!=coords.height)
		{
			tex->getPixels(tempbuf, GL_BGRA_EXT);
			CxImage *newImage = new CxImage(0);
			if (newImage) {
				newImage->AlphaCreate();	// Create the alpha layer
				newImage->IncreaseBpp(32);	// set image to 32bit
				newImage->CreateFromArray(tempbuf, tex->w, tex->h, 32, (tex->w*4), false);
				newImage->Resample(coords.width, coords.height, 2); // 0: hight quality, 1: normal quality
				wxDELETE(tempbuf);
				tempbuf = NULL;
				long size = coords.width * coords.height * 4;
				newImage->Encode2RGBA(tempbuf, size, false);
				wxDELETE(newImage);
			}
			else
			{
				free(tempbuf);
				continue;
			}
		}
		else
		{
			tex->getPixels(tempbuf);
		}

		// blit the texture region over the original
		for (ssize_t y=0, dy=coords.ypos; y<coords.height; y++,dy++)
		{
			for (ssize_t x=0, dx=coords.xpos; x<coords.width; x++,dx++)
			{
				unsigned char *src = tempbuf + y*coords.width*4 + x*4;
				unsigned char *dest = destbuf + dy*layoutInfos.first.width*4 + dx*4;

				// this is slow and ugly but I don't care
				// take into account alpha chanel
				float r = src[3] / 255.0f;
				float ir = 1.0f - r;
				// zomg RGBA?
				dest[0] = (unsigned char)(dest[0]*ir + src[0]*r);
				dest[1] = (unsigned char)(dest[1]*ir + src[1]*r);
				dest[2] = (unsigned char)(dest[2]*ir + src[2]*r);
				dest[3] = 255;
			}
		}

		free(tempbuf);
		texturemanager.del(temptex);
	}

	// good, upload this to video
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, layoutInfos.first.width, layoutInfos.first.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, destbuf);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);


	// debug write texture on disk
/*
	static int texIndex=0;
	QString name = QString("./ComposedTexture%1.png").arg(texIndex++);
	QImage FinalTexture(destbuf,layoutInfos.first.width, layoutInfos.first.height,QImage::Format_RGBA8888);
	FinalTexture.save(name);
*/
	free(destbuf);
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
    int curLayout = atoi(layouts.values[i][0].c_str());
    texLayout.width = atoi(layouts.values[i][1].c_str());
    texLayout.height = atoi(layouts.values[i][2].c_str());

    // search all regions for this layout
    QString query = QString("SELECT Section, X, Y, Width, Height  FROM CharComponentTextureSections WHERE LayoutID = %1").arg(curLayout);
    sqlResult regions = GAMEDATABASE.sqlQuery(query.toStdString());

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
      coords.xpos = atoi(regions.values[r][1].c_str());
      coords.ypos = atoi(regions.values[r][2].c_str());
      coords.width = atoi(regions.values[r][3].c_str());
      coords.height = atoi(regions.values[r][4].c_str());
      //std::cout << atoi(regions.values[r][0].c_str())+1 << " " << coords.xpos << " " << coords.ypos << " " << coords.width << " " << coords.height << std::endl;
      regionCoords[atoi(regions.values[r][0].c_str())+1] = coords;

    }
    LOG_INFO << "Found" << regionCoords.size() << "regions for layout" << curLayout;
    CharTexture::LAYOUTS[curLayout] = make_pair(texLayout,regionCoords);
  }

}

