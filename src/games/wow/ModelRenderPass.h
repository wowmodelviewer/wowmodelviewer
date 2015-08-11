/*
 * ModelRenderPass.h
 *
 *  Created on: 21 oct. 2013
 *
 */

#ifndef _MODELRENDERPASS_H_
#define _MODELRENDERPASS_H_

#include "quaternion.h"
#include "types.h"

class WoWModel;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _MODELRENDERPASS_API_ __declspec(dllexport)
#    else
#        define _MODELRENDERPASS_API_ __declspec(dllimport)
#    endif
#else
#    define _MODELRENDERPASS_API_
#endif


struct _MODELRENDERPASS_API_ ModelRenderPass {
	uint32 indexStart, indexCount, vertexStart, vertexEnd;
	//TextureID texture, texture2;
	int tex;
	bool useTex2, useEnvMap, cull, trans, unlit, noZWrite, billboard;
	float p;

	int16 texanim, color, opacity, blendmode;

	// Geoset ID
	int geoset;

	// texture wrapping
	bool swrap, twrap;

	// colours
	Vec4D ocol, ecol;

	bool init(WoWModel *m);
	void deinit();

	bool operator< (const ModelRenderPass &m) const
	{
		// This is the old sort order method which I'm pretty sure is wrong - need to try something else.
		// Althogh transparent part should be displayed later, but don't know how to sort it
		// And it will sort by geoset id now.
		return geoset < m.geoset;
	}
};


#endif /* _MODELRENDERPASS_H_ */
