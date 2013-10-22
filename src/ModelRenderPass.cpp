/*
 * ModelRenderPass.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "ModelRenderPass.h"

#include "enums.h"
#include "Model.h"
#include "ModelColor.h"
#include "ModelTransparency.h"
#include "TextureAnim.h"

#include "GL/glew.h"

void ModelRenderPass::deinit()
{

	switch (blendmode) {
	case BM_OPAQUE:
		break;
	case BM_TRANSPARENT:
		glDisable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GEQUAL,0.04f);

		/*
		glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
		glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB,GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_MODULATE);
		glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE,1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA,GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);
		*/

		break;

	case BM_ALPHA_BLEND:
		glDisable(GL_BLEND);
		break;
	case BM_ADDITIVE: // 3
 		glDisable(GL_BLEND);
		break;
	case BM_ADDITIVE_ALPHA: // 4
		//glDisable(GL_ALPHA_TEST);
 		glDisable(GL_BLEND);
		break;

	case BM_MODULATE: // 5
		glDisable(GL_BLEND);
		break;

	case BM_MODULATEX2: // 6
		glDisable(GL_BLEND);
		//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		break;
	default:
		glDisable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // default blend func

		/*
		// Default texture settings.
		glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
		glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE, 1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB, GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB, GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB, GL_MODULATE);
		glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE, 1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA, GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA, GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA, GL_REPLACE);
		*/
	}


	if (noZWrite)
		glDepthMask(GL_TRUE);

	if (texanim!=-1) {
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
	}

	if (unlit) {
		glEnable(GL_LIGHTING);
	}

	//if (billboard)
	//	glPopMatrix();

	if (cull)
		glDisable(GL_CULL_FACE);

	if (useEnvMap) {
		glDisable(GL_TEXTURE_GEN_S);
		glDisable(GL_TEXTURE_GEN_T);
	}

	if (swrap)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

	if (twrap)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	/*
	if (useTex2) {
		glDisable(GL_TEXTURE_2D);
		glActiveTextureARB(GL_TEXTURE0);
	}
	*/

	if (opacity!=-1 || color!=-1) {
		GLfloat czero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		glMaterialfv(GL_FRONT, GL_EMISSION, czero);

		//glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		//glMaterialfv(GL_FRONT, GL_AMBIENT, ocol);
		//ocol = Vec4D(1.0f, 1.0f, 1.0f, 1.0f);
		//glMaterialfv(GL_FRONT, GL_DIFFUSE, ocol);
	}
}


bool ModelRenderPass::init(Model *m)
{
	// May aswell check that we're going to render the geoset before doing all this crap.
	if (!m->showGeosets[geoset])
		return false;

	// COLOUR
	// Get the colour and transparency and check that we should even render
	ocol = Vec4D(1.0f, 1.0f, 1.0f, m->trans);
	ecol = Vec4D(0.0f, 0.0f, 0.0f, 0.0f);

	//if (m->trans == 1.0f)
	//	return false;

	// emissive colors
	if (color!=-1 && m->colors && m->colors[color].color.uses(0)) {
		Vec3D c;
		if (gameVersion >= VERSION_WOTLK) {
			/* Alfred 2008.10.02 buggy opacity make model invisable, TODO */
			c = m->colors[color].color.getValue(0,m->animtime);
			if (m->colors[color].opacity.uses(m->anim)) {
				ocol.w = m->colors[color].opacity.getValue(m->anim,m->animtime);
			}
		} else {
			c = m->colors[color].color.getValue(m->anim,m->animtime);
			ocol.w = m->colors[color].opacity.getValue(m->anim,m->animtime);
		}

		if (unlit) {
			ocol.x = c.x; ocol.y = c.y; ocol.z = c.z;
		} else {
			ocol.x = ocol.y = ocol.z = 0;
		}

		ecol = Vec4D(c, ocol.w);
		glMaterialfv(GL_FRONT, GL_EMISSION, ecol);
	}

	// opacity
	if (opacity!=-1) {
		if (gameVersion >= VERSION_WOTLK) {
			/* Alfred 2008.10.02 buggy opacity make model invisable, TODO */
			if (m->transparency && m->transparency[opacity].trans.uses(0))
				ocol.w *= m->transparency[opacity].trans.getValue(0, m->animtime);
		} else
			ocol.w *= m->transparency[opacity].trans.getValue(m->anim, m->animtime);
	}

	// exit and return false before affecting the opengl render state
	if (!((ocol.w > 0) && (color==-1 || ecol.w > 0)))
		return false;


	// TEXTURE
	// bind to our texture
	GLuint bindtex = 0;
	if (m->specialTextures[tex]==-1)
		bindtex = m->textures[tex];
	else
		bindtex = m->replaceTextures[m->specialTextures[tex]];

	glBindTexture(GL_TEXTURE_2D, bindtex);
	// --

	// TODO: Add proper support for multi-texturing.

	// ALPHA BLENDING
	// blend mode
	switch (blendmode) {
	case BM_OPAQUE:	// 0
		break;
	case BM_TRANSPARENT: // 1
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GEQUAL,0.7f);

		/*
		// Tex settings
		glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE,1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA,GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_MODULATE);
		*/

		break;
	case BM_ALPHA_BLEND: // 2
		//glDisable(GL_ALPHA_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // default blend func
		break;
	case BM_ADDITIVE: // 3
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_COLOR, GL_ONE);
		break;
	case BM_ADDITIVE_ALPHA: // 4
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		/*
		glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE,1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA,GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_MODULATE);
		*/

		break;
	case BM_MODULATE:	// 5
		glEnable(GL_BLEND);
		glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);

		/*
		// Texture settings.
		glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_INTERPOLATE);
		glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE,1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA,GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);
		*/

		break;
	case BM_MODULATEX2:	// 6, not sure if this is right
		glEnable(GL_BLEND);
		glBlendFunc(GL_DST_COLOR,GL_SRC_COLOR);

		/*
		// Texture settings.
		glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_INTERPOLATE);
		glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE,1.000000);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA,GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);
		*/

		break;
	default:
		wxLogMessage(wxT("[Error] Unknown blendmode: %d\n"), blendmode);
		glEnable(GL_BLEND);
		glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	}

	if (cull) {
		glEnable(GL_CULL_FACE);
	}

	// Texture wrapping around the geometry
	if (swrap)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	if (twrap)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// no writing to the depth buffer.
	if (noZWrite)
		glDepthMask(GL_FALSE);

	// Environmental mapping, material, and effects
	if (useEnvMap) {
		// Turn on the 'reflection' shine, using 18.0f as that is what WoW uses based on the reverse engineering
		// This is now set in InitGL(); - no need to call it every render.
		glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 18.0f);

		// env mapping
		glEnable(GL_TEXTURE_GEN_S);
		glEnable(GL_TEXTURE_GEN_T);

		const GLint maptype = GL_SPHERE_MAP;
		//const GLint maptype = GL_REFLECTION_MAP_ARB;

		glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, maptype);
		glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, maptype);
	}

	if (texanim!=-1) {
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();

		m->texAnims[texanim].setup(texanim);
	}

	// color
	glColor4fv(ocol);
	//glMaterialfv(GL_FRONT, GL_SPECULAR, ocol);

	// don't use lighting on the surface
	if (unlit) {
		glDisable(GL_LIGHTING);
	}

	if (blendmode<=1 && ocol.w<1.0f)
		glEnable(GL_BLEND);

	return true;
}
