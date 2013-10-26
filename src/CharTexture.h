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

struct CharRegionCoords {
	int xpos, ypos, xsize, ysize;
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

struct CharTexture
{
  size_t race;
	std::vector<CharTextureComponent> components;

  CharTexture(size_t _race)
    : race(_race)
  {}

	void addLayer(wxString fn, int region, int layer)
	{
		if (!fn || fn.length()==0)
			return;

		CharTextureComponent ct;
		ct.name = fn;
		ct.region = region;
		ct.layer = layer;
		components.push_back(ct);
	}
	void compose(TextureID texID);
};


#endif /* _CHARTEXTURE_H_ */
