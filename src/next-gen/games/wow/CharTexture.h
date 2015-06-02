/*
 * CharTexture.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#ifndef _CHARTEXTURE_H_
#define _CHARTEXTURE_H_

#include "video.h" // TextureID

#include <wx/string.h>

#include <map>

struct CharRegionCoords {
	int xpos, ypos, width, height;
};

struct LayoutSize {
  int width, height;
};

struct CharTextureComponent
{
	wxString name;
	int region;
	int layer;

	bool operator<(const CharTextureComponent& c) const
	{
		return layer < c.layer;
	}
};

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _CHARTEXTURE_API_ __declspec(dllexport)
#    else
#        define _CHARTEXTURE_API_ __declspec(dllimport)
#    endif
#else
#    define _CHARTEXTURE_API_
#endif

class _CHARTEXTURE_API_ CharTexture
{
  public:
    CharTexture(size_t _layoutSizeId = 0)
        : layoutSizeId(_layoutSizeId)
      {}

    void addLayer(wxString fn, int region, int layer);

    void compose(TextureID texID);

    void reset(size_t _layoutSizeId);

    static void initRegions();

  private:
    size_t layoutSizeId;
	  std::vector<CharTextureComponent> m_components;
	  static std::map<int, pair<LayoutSize, std::map<int,CharRegionCoords> > > LAYOUTS;

};


#endif /* _CHARTEXTURE_H_ */
