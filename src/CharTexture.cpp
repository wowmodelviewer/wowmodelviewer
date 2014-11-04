/*
 * CharTexture.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "CharTexture.h"

#include "enums.h" // NUM_REGIONS

#include "CxImage/ximage.h"

#define	REGION_FAC_X	2
#define REGION_FAC_Y  2
#define	REGION_PX_WIDTH	(256*REGION_FAC_X)
#define REGION_PX_HEIGHT (256*REGION_FAC_Y)

const CharRegionCoords regions[NUM_REGIONS] =
{
	{0, 0, 256*REGION_FAC_X, 256*REGION_FAC_Y},	// base
	{0, 0, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// arm upper
	{0, 64*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// arm lower
	{0, 128*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// hand
	{0, 160*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// face upper
	{0, 192*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// face lower
	{128*REGION_FAC_X, 0, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// torso upper
	{128*REGION_FAC_X, 64*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// torso lower
	{128*REGION_FAC_X, 96*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y}, // pelvis upper
	{128*REGION_FAC_X, 160*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},// pelvis lower
	{128*REGION_FAC_X, 224*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y}	// foot
};

const CharRegionCoords pandaren_regions[NUM_REGIONS] =
{
  {0, 0, 256*REGION_FAC_X*2, 256*REGION_FAC_Y},	// base
  {0, 0, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// arm upper
  {0, 64*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// arm lower
  {0, 128*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// hand
  {128*REGION_FAC_X*2, 0, 256*REGION_FAC_X, 256*REGION_FAC_Y},	// face upper
  {0, 192*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// face lower
  {128*REGION_FAC_X, 0, 128*REGION_FAC_X, 64*REGION_FAC_Y},	// torso upper
  {128*REGION_FAC_X, 64*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y},	// torso lower
  {128*REGION_FAC_X, 96*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y}, // pelvis upper
  {128*REGION_FAC_X, 160*REGION_FAC_Y, 128*REGION_FAC_X, 64*REGION_FAC_Y},// pelvis lower
  {128*REGION_FAC_X, 224*REGION_FAC_Y, 128*REGION_FAC_X, 32*REGION_FAC_Y}	// foot
};

// 2007.07.03 Alfred, enlarge buf size and make it static to prevent stack overflow
//static unsigned char destbuf[REGION_PX*REGION_PX*4], tempbuf[REGION_PX*REGION_PX*4];
void CharTexture::compose(TextureID texID)
{
  // scale for pandaren race.
  size_t x_scale = race == 24 ? 2 : 1;
  size_t y_scale = 1;

	// if we only have one texture then don't bother with compositing
	if (components.size()==1) {
		Texture temp(components[0].name);
		texturemanager.LoadBLP(texID, &temp);
		return;
	}

	std::sort(components.begin(), components.end());

	unsigned char *destbuf, *tempbuf;
	destbuf = (unsigned char*)malloc(REGION_PX_WIDTH*x_scale*REGION_PX_HEIGHT*y_scale*4);
	memset(destbuf, 0, REGION_PX_WIDTH*x_scale*REGION_PX_HEIGHT*y_scale*4);

	for (std::vector<CharTextureComponent>::iterator it = components.begin(); it != components.end(); ++it) {
		CharTextureComponent &comp = *it;
    // pandaren with different regions.
		const CharRegionCoords &coords = race == 24 ? pandaren_regions[comp.region] : regions[comp.region];
		TextureID temptex = texturemanager.add(comp.name);
		Texture &tex = *((Texture*)texturemanager.items[temptex]);

		// Alfred 2009.07.03, tex width or height can't be zero
		if (tex.w == 0 || tex.h == 0) {
			texturemanager.del(temptex);
			continue;
		}
		tempbuf = (unsigned char*)malloc(tex.w*tex.h*4);
		if (!tempbuf)
			continue;
		memset(tempbuf, 0, tex.w*tex.h*4);

		if (tex.w!=coords.xsize || tex.h!=coords.ysize)
		{
			tex.getPixels(tempbuf, GL_BGRA_EXT);
			CxImage *newImage = new CxImage(0);
			if (newImage) {
				newImage->AlphaCreate();	// Create the alpha layer
				newImage->IncreaseBpp(32);	// set image to 32bit
				newImage->CreateFromArray(tempbuf, tex.w, tex.h, 32, (tex.w*4), false);
				newImage->Resample(coords.xsize, coords.ysize, 0); // 0: hight quality, 1: normal quality
				wxDELETE(tempbuf);
				tempbuf = NULL;
				long size = coords.xsize * coords.ysize * 4;
				newImage->Encode2RGBA(tempbuf, size, false);
				wxDELETE(newImage);
			} else {
				free(tempbuf);
				continue;
			}
		} else
			tex.getPixels(tempbuf);

		// blit the texture region over the original
		for (ssize_t y=0, dy=coords.ypos; y<coords.ysize; y++,dy++) {
			for (ssize_t x=0, dx=coords.xpos; x<coords.xsize; x++,dx++) {
				unsigned char *src = tempbuf + y*coords.xsize*4 + x*4;
				unsigned char *dest = destbuf + dy*REGION_PX_WIDTH*x_scale*4 + dx*4;

				// this is slow and ugly but I don't care
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
	//	texturemanager.del(temptex);
	}

	// good, upload this to video
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, REGION_PX_WIDTH*x_scale, REGION_PX_HEIGHT*y_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, destbuf);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	free(destbuf);
}
