/*
 * ModelRenderPass.cpp
 *
 *  Created on: 21 oct. 2013
 *
 */

#include "ModelRenderPass.h"

#include "ModelColor.h"
#include "ModelTransparency.h"
#include "TextureAnim.h"
#include "wow_enums.h"
#include "WoWModel.h"
#include "logger/Logger.h"
#include "GL/glew.h"

ModelRenderPass::ModelRenderPass(WoWModel * m):
  useTex2(false), useEnvMap(false), cull(false), trans(false), 
  unlit(false), noZWrite(false), billboard(false),
  texanim(-1), color(-1), opacity(-1), blendmode(-1),
  swrap(false), twrap(false), ocol(0.0f, 0.0f, 0.0f, 0.0f), ecol(0.0f, 0.0f, 0.0f, 0.0f),
  model(m), geoIndex(-1)
{

}

void ModelRenderPass::deinit()
{
  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (noZWrite)
    glDepthMask(GL_TRUE);

  if (texanim!=-1)
  {
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
  }

  if (unlit)
    glEnable(GL_LIGHTING);

  //if (billboard)
  //	glPopMatrix();

  if (cull)
    glDisable(GL_CULL_FACE);

  if (useEnvMap)
  {
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
  }

  if (swrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

  if (twrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /*
    if (useTex2)
    {
      glDisable(GL_TEXTURE_2D);
      glActiveTextureARB(GL_TEXTURE0);
    }
   */

  if (opacity!=-1 || color!=-1)
  {
    GLfloat czero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glMaterialfv(GL_FRONT, GL_EMISSION, czero);

    //glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    //glMaterialfv(GL_FRONT, GL_AMBIENT, ocol);
    //ocol = Vec4D(1.0f, 1.0f, 1.0f, 1.0f);
    //glMaterialfv(GL_FRONT, GL_DIFFUSE, ocol);
  }
}


void ModelRenderPass::init()
{
  // May as well check that we're going to render the geoset before doing all this crap.
  if (!displayed())
    return;

  // COLOUR
  // Get the colour and transparency and check that we should even render
  ocol = Vec4D(1.0f, 1.0f, 1.0f, model->trans);
  ecol = Vec4D(0.0f, 0.0f, 0.0f, 0.0f);

  // emissive colors
  if (color != -1 && color < (int16)model->colors.size() && model->colors[color].color.uses(0))
  {
    Vec3D c;
    /* Alfred 2008.10.02 buggy opacity make model invisible, TODO */
    c = model->colors[color].color.getValue(0, model->animtime);
    if (model->colors[color].opacity.uses(model->anim))
      ocol.w = model->colors[color].opacity.getValue(model->anim, model->animtime);

    if (unlit)
    {
      ocol.x = c.x; ocol.y = c.y; ocol.z = c.z;
    }
    else
      ocol.x = ocol.y = ocol.z = 0;

    ecol = Vec4D(c, ocol.w);
    glMaterialfv(GL_FRONT, GL_EMISSION, ecol);
  }

  // opacity
  if (opacity != -1 && 
      opacity < (int16)model->textureWeights.size() && 
      model->textureWeights[opacity].weight.uses(0))
  {
    // Alfred 2008.10.02 buggy opacity make model invisible, TODO
    ocol.w *= model->textureWeights[opacity].weight.getValue(0, model->animtime);
  }

  // exit and return false before affecting the opengl render state
  if (!((ocol.w > 0) && (color == -1 || ecol.w > 0)))
    return;


  // TEXTURE
  // bind to our texture
  GLuint texId = model->getGLTexture(texs[0]);
  if (texId != INVALID_TEX)
    glBindTexture(GL_TEXTURE_2D, texId);

  // ALPHA BLENDING
  // blend mode
  switch (blendmode)
  {
  case BM_OPAQUE:	         // 0
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BM_TRANSPARENT:      // 1
    glEnable(GL_ALPHA_TEST);
    glBlendFunc(GL_ONE, GL_ZERO);
	break;
  case BM_ALPHA_BLEND:      // 2
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case BM_ADDITIVE:         // 3
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_COLOR, GL_ONE);
    break;
  case BM_ADDITIVE_ALPHA:   // 4
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    break;
  case BM_MODULATE:	         // 5
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    break;
  case BM_MODULATEX2:	    // 6
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    break;
  case BM_7:	               // 7, new in WoD
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    break;
  default:
    LOG_ERROR << "Unknown blendmode:" << blendmode;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  if (cull)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
  // no writing to the depth buffer.
  if (noZWrite)
    glDepthMask(GL_FALSE);
  else
    glDepthMask(GL_TRUE);

  // Texture wrapping around the geometry
  if (swrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  if (twrap)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  // Environmental mapping, material, and effects
  if (useEnvMap)
  {
    // Turn on the 'reflection' shine, using 18.0f as that is what WoW uses based on the reverse engineering
    // This is now set in InitGL(); - no need to call it every render.
    // glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 18.0f);

    // env mapping
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);

    const GLint maptype = GL_SPHERE_MAP;
    //const GLint maptype = GL_REFLECTION_MAP_ARB;

    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, maptype);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, maptype);
  }

  if (texanim != -1 &&
      texanim < (int16)model->texAnims.size())
  {
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();

    model->texAnims[texanim].setup(texanim);
  }

  // color
  glColor4fv(ocol);
  //glMaterialfv(GL_FRONT, GL_SPECULAR, ocol);

  // don't use lighting on the surface
  if (unlit)
    glDisable(GL_LIGHTING);

  if (blendmode <= 1 && ocol.w < 1.0f)
    glEnable(GL_BLEND);

  texId = model->getGLTexture(texs[2]);
  if (texId != INVALID_TEX)
    glBindTexture(GL_TEXTURE_2D, texId);
}

void ModelRenderPass::init(uint16 tex)
{ 
  // ALPHA BLENDING
  // blend mode
  switch (materials[tex].blend)
  {
    case BM_OPAQUE:	          // 0
      glDisable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      break;
    case BM_TRANSPARENT:      // 1
      glDisable(GL_BLEND);
      glEnable(GL_ALPHA_TEST);
      glBlendFunc(GL_ONE, GL_ZERO);
      break;
    case BM_ALPHA_BLEND:      // 2
      // @TODO : buggy blendmode 2 management for now, return
      return;
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case BM_ADDITIVE:         // 3
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_SRC_COLOR, GL_ONE);
      break;
    case BM_ADDITIVE_ALPHA:   // 4
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      break;
    case BM_MODULATE:	      // 5
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_DST_COLOR, GL_ZERO);
      break;
    case BM_MODULATEX2:	      // 6
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
      break;
    case BM_7:	              // 7, new in WoD
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    default:
      LOG_ERROR << "Unknown blendmode:" << materials[tex].blend;
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
  }

  uint16 flags = materials[tex].flags;

  if((flags & M2MATERIAL_UNLIT) != 0)
    glDisable(GL_LIGHTING);
  else
    glEnable(GL_LIGHTING);

  if((flags & M2MATERIAL_TWOSIDED) != 0)
    glDisable(GL_CULL_FACE);
  else
    glEnable(GL_CULL_FACE);

  // no writing to the depth buffer.
  if ((flags & M2MATERIAL_ZBUFFERED) != 0)
    glDepthMask(GL_FALSE);
  else
    glDepthMask(GL_TRUE);

  //billboard = (rf.flags & M2MATERIAL_BILLBOARD) != 0;

  // Texture wrapping around the geometry
  if ((textureDefs[texs[tex]].flags & MODELTEXTUREDEF_WRAP_X) != 0)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);

  if ((textureDefs[texs[tex]].flags & MODELTEXTUREDEF_WRAP_Y) != 0)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);


  GLuint texId = model->getGLTexture(texs[tex]);
  if (texId != INVALID_TEX)
    glBindTexture(GL_TEXTURE_2D, texId);
}



void ModelRenderPass::render()
{
  if (!displayed())
    return;

  for (uint i = 0; i < texs.size(); i++)
  {
    init(i);
    draw(i);
  }
  deinit();
}

void ModelRenderPass::setupFromM2Batch(M2Batch & batch)
{
  if (!model)
    return;

  ModelTextureDef *texdef = (ModelTextureDef*)(model->gamefile->getBuffer() + model->header.ofsTextures);
  int16 *transLookup = (int16*)(model->gamefile->getBuffer() + model->header.ofsTransparencyLookup);
  M2Material *renderFlags = (M2Material*)(model->gamefile->getBuffer() + model->header.ofsMaterials);
  uint16 *texlookup = (uint16*)(model->gamefile->getBuffer() + model->header.ofsTextureLookup);
  int16 *texanimlookup = (int16*)(model->gamefile->getBuffer() + model->header.ofsTextureTransformLookup);
  int16 *texunitlookup = (int16*)(model->gamefile->getBuffer() + model->header.ofsTextureUnitLookup);

  M2TextureWeight *trs = (M2TextureWeight*)(model->gamefile->getBuffer() + model->header.ofsTextureWeights);
  for (size_t i = 0; i < model->header.nTextureWeights; i++)
  {
    LOG_INFO << "trs" << i << "type" << trs[i].weight.type << "nKeys" << trs[i].weight.nKeys << "nTimes" << trs[i].weight.nTimes;
  }

  LOG_INFO << "texture lookup";
  for (uint i = 0; i < model->header.nTextureLookup; i++)
    LOG_INFO << i << texlookup[i];

  LOG_INFO << "materials";
  for (uint i = 0; i < model->header.nMaterials; i++)
    LOG_INFO << i << renderFlags[i].blend << hex << renderFlags[i].flags;

  LOG_INFO << "texture transform lookup";
  for (uint i = 0; i < model->header.nTextureTransformLookup; i++)
    LOG_INFO << i << texanimlookup[i];

  LOG_INFO << "texture transparency lookup";
  for (uint i = 0; i < model->header.nTransparencyLookup; i++)
    LOG_INFO << i << transLookup[i];

  LOG_INFO << "texture unit lookup";
  for (uint i = 0; i < model->header.nTextureUnitLookup; i++)
    LOG_INFO << i << texunitlookup[i];


  geoIndex = batch.skinSectionIndex;
 
  for (uint i = 0; i < batch.textureCount; i++)
  {
    LOG_INFO << "-----------------------";
    LOG_INFO << "texture" << i;
    texs.push_back(texlookup[batch.textureComboIndex + i]);
    LOG_INFO << "tex" << texs[i] << model->getNameForTex(texs[i]);
    textureDefs.push_back(texdef[batch.textureComboIndex + i]);
    LOG_INFO << "flags" << hex << textureDefs[i].flags;
    LOG_INFO << "type" << hex << textureDefs[i].type;
    materials.push_back(renderFlags[batch.materialIndex + i]);
    LOG_INFO << "blendmode" << materials[i].blend;
    LOG_INFO << "material flags" << hex << materials[i].flags;
    textureTransforms.push_back(texanimlookup[batch.textureTransformComboIndex + i]);
    LOG_INFO << "transform" << textureTransforms[i];

  }

  LOG_INFO << "video.supportVBO" << video.supportVBO << "video.supportDrawRangeElements" << video.supportDrawRangeElements;

  M2Material &rf = renderFlags[batch.materialIndex];

  blendmode = rf.blend;
  //if (rf.blend == 0) // Test to disable/hide different blend types
  //	continue;

  color = batch.colorIndex;

  opacity = transLookup[batch.textureWeightComboIndex];

  unlit = (rf.flags & M2MATERIAL_UNLIT) != 0;

  cull = (rf.flags & M2MATERIAL_TWOSIDED) == 0;

  billboard = (rf.flags & M2MATERIAL_BILLBOARD) != 0;

  // Use environmental reflection effects?
  useEnvMap = (texunitlookup[batch.textureCoordComboIndex] == -1) && billboard && rf.blend > 2; //&& rf.blend<5;

  // Disable environmental mapping if its been unchecked.
  if (useEnvMap && !video.useEnvMapping)
    useEnvMap = false;

  noZWrite = (rf.flags & M2MATERIAL_ZBUFFERED) != 0;

  // ToDo: Work out the correct way to get the true/false of transparency
  trans = (blendmode > 0) && (opacity > 0);	// Transparency - not the correct way to get transparency

  // Texture flags
  swrap = (texdef[texs[0]].flags & MODELTEXTUREDEF_WRAP_X) != 0; // Texture wrap X
  twrap = (texdef[texs[0]].flags & MODELTEXTUREDEF_WRAP_Y) != 0; // Texture wrap Y

  // tex->flags: Usually 16 for static textures, and 0 for animated textures.
  if ((batch.flags & M2BATCH_STATIC) == 0)
  {
    texanim = texanimlookup[batch.textureTransformComboIndex];
  }
}

bool ModelRenderPass::displayed()
{
  if(!model || geoIndex == -1 || !model->geosets[geoIndex]->display)
    return false;

  return true;
}

void ModelRenderPass::draw(uint16 tex)
{
  M2SkinSectionHD * geoset = model->geosets[geoIndex];
  if (video.supportVBO && video.supportDrawRangeElements)
  {

    //glDrawElements(GL_TRIANGLES, p.indexCount, GL_UNSIGNED_SHORT, indices + p.indexStart);
    // a GDC OpenGL Performace Tuning paper recommended glDrawRangeElements over glDrawElements
    // I can't notice a difference but I guess it can't hurt
    glDrawRangeElements(GL_TRIANGLES, geoset->vertexStart, geoset->vertexStart + geoset->vertexCount, geoset->indexCount, GL_UNSIGNED_SHORT, &model->indices[geoset->indexStart]);
  }
  else
  {
    glBegin(GL_TRIANGLES);
    for (size_t k = 0, b = geoset->indexStart; k < geoset->indexCount; k++, b++)
    {
      uint32 a = model->indices[b];
      glNormal3fv(model->normals[a]);
      if(tex != 1)
        glTexCoord2fv(model->origVertices[a].texcoords[0]);
      else
        glTexCoord2fv(model->origVertices[a].texcoords[1]);
      glVertex3fv(model->vertices[a]);
    }
    glEnd();
  }
}

