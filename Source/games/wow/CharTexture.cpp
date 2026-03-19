#include "CharTexture.h"

#include <algorithm>
#include <map>
#include <string>

#include "DB2Table.h"
#include "Game.h"
#include "GameFile.h"
#include "Texture.h"
#include "TextureManager.h"
#include "WoWDatabase.h"

#include "logger/Logger.h"

std::map<int, std::pair<LayoutSize, std::map<int, CharRegionCoords>>> CharTexture::LAYOUTS;
constexpr int LAYOUT_BASE_REGION = -1;

void CharTexture::addLayer(GameFile* file, int region, int layer, int blendMode)
{
	if (!file)
		return;

	const CharTextureComponent ct = {file, region, layer, blendMode};
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

	SoftwareImage img;

#if DEBUG_TEXTURE > 1
  static auto baseidx = 0;
  auto cmpidx = 0;
#endif

	for (auto it : m_components)
	{
		burnComponent(img, it);
#if DEBUG_TEXTURE > 1
	img.savePNG("./ComposedTexture" + std::to_string(baseidx) + "_" + std::to_string(cmpidx++) + ".png");
#endif
	}

	// good, upload this to video
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_BGRA, GL_UNSIGNED_BYTE, img.data());
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}

void CharTexture::initRegions()
{
	const DB2Table* layoutsTbl = WOWDB.getTable("CharComponentTextureLayouts");
	if (!layoutsTbl || layoutsTbl->getRowCount() == 0)
	{
		LOG_ERROR << "Fail to retrieve Texture Layout information from game database";
		return;
	}

	const DB2Table* sectionsTbl = WOWDB.getTable("CharComponentTextureSections");
	if (!sectionsTbl || sectionsTbl->getRowCount() == 0)
	{
		LOG_ERROR << "Fail to retrieve Section Layout information from game database";
		return;
	}

	// Iterate on layout to initialize our members (sections informations)
	for (const auto& layoutRow : *layoutsTbl)
	{
		const int curLayout = static_cast<int>(layoutRow.recordID());
		LayoutSize texLayout = {static_cast<int>(layoutRow.getUInt("Width")),
								static_cast<int>(layoutRow.getUInt("Height"))};

		std::map<int, CharRegionCoords> regionCoords;
		const CharRegionCoords base = {0, 0, texLayout.width, texLayout.height};
		regionCoords[LAYOUT_BASE_REGION] = base;

		// search all regions for this layout
		bool found = false;
		for (const auto& secRow : *sectionsTbl)
		{
			if (static_cast<int>(secRow.getUInt("CharComponentTextureLayoutID")) != curLayout)
				continue;

			found = true;
			const CharRegionCoords coords = {
				static_cast<int>(secRow.getUInt("X")),
				static_cast<int>(secRow.getUInt("Y")),
				static_cast<int>(secRow.getUInt("Width")),
				static_cast<int>(secRow.getUInt("Height"))
			};
			regionCoords[static_cast<int>(secRow.getUInt("SectionType"))] = coords;
		}

		if (!found)
		{
			LOG_ERROR << "Fail to retrieve Section Layout information from game database for layout" << curLayout;
			continue;
		}

		LOG_INFO << "Found" << regionCoords.size() << "regions for layout" << curLayout;
		CharTexture::LAYOUTS[curLayout] = make_pair(texLayout, regionCoords);
	}
}

void CharTexture::burnComponent(SoftwareImage& destImage, CharTextureComponent& ct) const
{
	auto layoutInfos = CharTexture::LAYOUTS[layoutSizeId];

	const auto& coords = layoutInfos.second[ct.region];

	const auto* tmp = gameFileToQImage(ct.file);

	if (!tmp)
		return;

	const auto newImage = tmp->scaled(coords.width, coords.height);


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
  newImage.savePNG("./tex__" + std::to_string(region) + "_" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(width) + "_" + std::to_string(height) + ".png");
#endif

	if ((ct.region == LAYOUT_BASE_REGION
			|| ct.region == 11) // Dracthyr dragon base section
		&& ct.layer == 0)
	{
		destImage.assign(newImage);
	}
	else
	{
		destImage.composite(newImage, coords.xpos, coords.ypos, ct.blendMode);
	}

	delete tmp;
}

SoftwareImage* CharTexture::gameFileToQImage(GameFile* file)
{
	SoftwareImage* result = nullptr;
	const auto temptex = TEXTUREMANAGER.add(file);
	auto* tex = dynamic_cast<Texture*>(TEXTUREMANAGER.items[temptex]);

	// Alfred 2009.07.03, tex width or height can't be zero
	if (!tex || tex->w == 0 || tex->h == 0)
	{
		TEXTUREMANAGER.del(temptex);
		return result;
	}

	auto* tempbuf = static_cast<unsigned char*>(malloc(tex->w * tex->h * 4));

	tex->getPixels(tempbuf, GL_BGRA);
	result = new SoftwareImage(tempbuf, tex->w, tex->h);

	free(tempbuf);
	TEXTUREMANAGER.del(temptex);

	return result;
}
