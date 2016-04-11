#include "WMOGroup.h"

#include "CASCFile.h"
#include "types.h"
#include "WMO.h"



#include <QString>

/*
The fields referenced from the MOPR chunk indicate portals leading out of the WMO group in question.
For the "Number of batches" fields, A + B + C == the total number of batches in the WMO group (in the MOBA chunk). This might be some kind of LOD thing, or just separating the batches into different types/groups...?
Flags: always contain more information than flags in MOGI. I suppose MOGI only deals with topology/culling, while flags here also include rendering info.
Flag		Meaning
0x1 		something with bounding
0x4 		Has vertex colors (MOCV chunk)
0x8 		Outdoor
0x40
0x200 		Has lights  (MOLR chunk)
0x400
0x800 		Has doodads (MODR chunk)
0x1000 	        Has water   (MLIQ chunk)
0x2000		Indoor
0x8000
0x20000		Parses some additional chunk. No idea what it is. Oo
0x40000	        Show skybox
0x80000		isNotOcean, LiquidType related, see below in the MLIQ chunk.
*/
struct WMOGroupHeader {
  int nameStart; // Group name (offset into MOGN chunk)
  int nameStart2; // Descriptive group name (offset into MOGN chunk)
  int flags;
  float box1[3]; // Bounding box corner 1 (same as in MOGI)
  float box2[3]; // Bounding box corner 2
  short portalStart; // Index into the MOPR chunk
  short portalCount; // Number of items used from the MOPR chunk
  short batches[4];
  uint8 fogs[4]; // Up to four indices into the WMO fog list
  int32 unk1; // LiquidType related, see below in the MLIQ chunk.
  int32 id; // WMO group ID (column 4 in WMOAreaTable.dbc)
  int32 unk2; // Always 0?
  int32 unk3; // Always 0?
};

void setGLColor(unsigned int col)
{
  //glColor4ubv((GLubyte*)(&col));
  GLubyte r, g, b;
  r = (col & 0x00FF0000) >> 16;
  g = (col & 0x0000FF00) >> 8;
  b = (col & 0x000000FF);
  glColor4ub(r, g, b, 1);
}


void WMOGroup::initDisplayList()
{
  vertices = NULL;
  normals = NULL;
  texcoords = NULL;
  indices = NULL;
  materials = NULL;
  batches = 0;
  nBatches = 0;

  WMOGroupHeader gh;

  short *useLights = 0;
  int nLR = 0;

  // open group file
  QString temp(wmo->itemName());
  temp = temp.left(temp.lastIndexOf('.'));

  QString fname = QString("%1_%2.wmo").arg(temp).arg(num, 3, 10, QChar('0'));
  CASCFile gf(fname);
  gf.open();
  gf.seek(0x14);

  // read header
  gf.read(&gh, sizeof(WMOGroupHeader));
  WMOFog &wf = wmo->fogs[gh.fogs[0]];
  if (wf.r2 <= 0)
    fog = -1; // default outdoor fog..?
  else
    fog = gh.fogs[0];

  name = QString::fromLatin1(wmo->groupnames + gh.nameStart).toStdString();
  desc = QString::fromLatin1(wmo->groupnames + gh.nameStart2).toStdString();

  b1 = Vec3D(gh.box1[0], gh.box1[2], -gh.box1[1]);
  b2 = Vec3D(gh.box2[0], gh.box2[2], -gh.box2[1]);

  gf.seek(0x58); // first chunk

  uint32 size;

  cv = 0;
  hascv = false;

  while (!gf.isEof()) {
    char buf[5];
    gf.read(buf, 4);
    buf[4] = 0;

    QString fourcc = QString::fromLatin1(buf);
    WMO::flipcc(fourcc);

    gf.read(&size, 4);

    size_t nextpos = gf.getPos() + size;

    // why copy stuff when I can just map it from memory ^_^

    if (fourcc == "MOPY") {

      //			Material info for triangles, two bytes per triangle. So size of this chunk in bytes is twice the number of triangles in the WMO group.
      //			Offset	Type	Description
      //			0x00	uint8	Flags?
      //			0x01	uint8	Material ID
      //			struct SMOPoly // 03-29-2005 By ObscuR ( Maybe not accurate :p )
      //			{
      //				enum
      //				{
      //					F_NOCAMCOLLIDE,
      //					F_DETAIL,
      //					F_COLLISION,
      //					F_HINT,
      //					F_RENDER,
      //					F_COLLIDE_HIT,
      //				};
      //			000h  uint8 flags;
      //			001h  uint8 mtlId;
      //			002h
      //			};
      //
      //			Frequently used flags are 0x20 and 0x40, but I have no idea what they do.
      //			Flag	Description
      //			0x00	?
      //			0x01	?
      //			0x04	no collision
      //			0x08	?
      //			0x20	?
      //			0x40	?
      //			Material ID specifies an index into the material table in the root WMO file's MOMT chunk. Some of the triangles have 0xFF for the material ID, I skip these. (but there might very well be a use for them?)
      //			The triangles with 0xFF Material ID seem to be a simplified mesh. Like for collision detection or something like that. At least stairs are flattened to ramps if you only display these polys. --shlainn 7 Jun 2009
      //			0xFF representing -1 is used for collision-only triangles. They aren't rendered but have collision. Problem with it: WoW seems to cast and reflect light on them. Its a bug in the engine. --schlumpf_ 20:40, 7 June 2009 (CEST)
      //			Triangles stored here are more-or-less pre-sorted by texture, so it's ok to draw them sequentially.

      // materials per triangle
      nTriangles = (uint32)(size / 2);
      materials = new uint16[nTriangles];
      gf.read(materials, size);
    }
    else if (fourcc == "MOVI") {
      //			Vertex indices for triangles. Three 16-bit integers per triangle, that are indices into the vertex list. The numbers specify the 3 vertices for each triangle, their order makes it possible to do backface culling.
      nIndices = (size / 2);
      indices = new uint16[nIndices];
      gf.read(indices, nIndices * 2);
    }
    else if (fourcc == "MOVT") {
      //			Vertices chunk. 3 floats per vertex, the coordinates are in (X,Z,-Y) order. It's likely that WMOs and models (M2s) were created in a coordinate system with the Z axis pointing up and the Y axis into the screen, whereas in OpenGL, the coordinate system used in WoWmapview the Z axis points toward the viewer and the Y axis points up. Hence the juggling around with coordinates.
      nVertices = (size / 12);
      // let's hope it's padded to 12 bytes, not 16...
      vertices = new Vec3D[nVertices];
      gf.read(vertices, size);
      vmin = Vec3D(9999999.0f, 9999999.0f, 9999999.0f);
      vmax = Vec3D(-9999999.0f, -9999999.0f, -9999999.0f);
      rad = 0;
      for (size_t i = 0; i < nVertices; i++) {
        Vec3D v(vertices[i].x, vertices[i].z, -vertices[i].y);
        if (v.x < vmin.x) vmin.x = v.x;
        if (v.y < vmin.y) vmin.y = v.y;
        if (v.z < vmin.z) vmin.z = v.z;
        if (v.x > vmax.x) vmax.x = v.x;
        if (v.y > vmax.y) vmax.y = v.y;
        if (v.z > vmax.z) vmax.z = v.z;
      }
      center = (vmax + vmin) * 0.5f;
      rad = (vmax - center).length();
    }
    else if (fourcc == "MONR") {
      // Normals. 3 floats per vertex normal, in (X,Z,-Y) order.
      uint32 tSize = (uint32)(size / 12);
      normals = new Vec3D[tSize];
      gf.read(normals, size);
    }
    else if (fourcc == "MOTV") {
      // Texture coordinates, 2 floats per vertex in (X,Y) order. The values range from 0.0 to 1.0. Vertices, normals and texture coordinates are in corresponding order, of course.
      uint32 tSize = (uint32)(size / 8);
      texcoords = new Vec2D[tSize];
      gf.read(texcoords, size);
    }
    else if (fourcc == "MOLR") {
      //			Light references, one 16-bit integer per light reference.
      //			This is basically a list of lights used in this WMO group, the numbers are indices into the WMO root file's MOLT table.
      //			For some WMO groups there is a large number of lights specified here, more than what a typical video card will handle at once. I wonder how they do lighting properly. Currently, I just turn on the first GL_MAX_LIGHTS and hope for the best. :(
      nLR = (int)size / 2;
      useLights = (short*)gf.getPointer();
    }
    else if (fourcc == "MODR") {
      //			Doodad references, one 16-bit integer per doodad.
      //			The numbers are indices into the doodad instance table (MODD chunk) of the WMO root file. These have to be filtered to the doodad set being used in any given WMO instance.
      /*
      nDoodads = (int)(size/2);
      ddr = new short[nDoodads];
      gf.read(ddr,size);
      */
    }
    else if (fourcc == "MOBN") {
      //			Array of t_BSP_NODE.
      //			struct t_BSP_NODE
      //			{
      //				short planetype;		  // unsure
      //				short children[2];		  // index of bsp child node(right in this array)
      //				unsigned short numfaces;  // num of triangle faces in  MOBR
      //				unsigned short firstface; // index of the first triangle index(in  MOBR)
      //				short nUnk; 		  // 0
      //				float fDist;
      //			};
      //			// The numfaces and firstface define a polygon plane.
      //													2005-4-4 by linghuye
      //			This+BoundingBox(in wmo_root.MOGI) is used for Collision --Tigurius
    }
    else if (fourcc == "MOBR") {
      // Triangle indices (in MOVI which define triangles) to describe polygon planes defined by MOBN BSP nodes.
    }
    else if (fourcc == "MOBA") {
      //			Render batches. Records of 24 bytes.
      //			struct SMOBatch // 03-29-2005 By ObscuR
      //			{
      //				enum
      //				{
      //					F_RENDERED
      //				};
      //				?? lightMap;
      //				?? texture;
      //				?? bx;
      //				?? by;
      //				?? bz;
      //				?? tx;
      //				?? ty;
      //				?? tz;
      //				?? startIndex;
      //				?? count;
      //				?? minIndex;
      //				?? maxIndex;
      //				?? flags;
      //			};
      //			For the enUS, enGB versions, it seems to be different from the preceding struct:
      //			Offset	Type		Description
      //			0x00	uint32		Some color?
      //			0x04	uint32		Some color?
      //			0x08	uint32		Some color?
      //			0x0C	uint32		Start index
      //			0x10	uint16		Number of indices
      //			0x12	uint16		Start vertex
      //			0x14	uint16		End vertex
      //			0x16	uint8		0?
      //			0x17	uint8		Texture
      //
      //			Flags
      //			0x1		Unknown
      //			0x4		Unknown

      nBatches = (uint32)(size / 24);
      batches = new WMOBatch[nBatches];
      gf.read(batches, size);

      //			// batch logging
      //			gLog("\nWMO group #%d - %s\nVertices: %d\nTriangles: %d\nIndices: %d\nBatches: %d\n",
      //				this->num, this->name.c_str(), nVertices, nTriangles, nTriangles*3, nBatches);
      //			WMOBatch *ba = batches;
      //			for (size_t i=0; i<nBatches; i++) {
      //				gLog("Batch %d:\t", i);
      //
      //				for (size_t j=0; j<12; j++) {
      //					if ((j%4)==0 && j!=0) gLog("| ");
      //					gLog("%d\t", ba[i].bytes[j]);
      //				}
      //
      //				gLog("| %d\t%d\t| %d\t%d\t", ba[i].indexStart, ba[i].indexCount, ba[i].vertexStart, ba[i].vertexEnd);
      //				gLog("%d\t%d\t%s\n", ba[i].flags, ba[i].texture, wmo->textures[ba[i].texture].c_str());
      //
      //			}
      //			int l = nBatches-1;
      //			gLog("Max index: %d\n", ba[l].indexStart + ba[l].indexCount);
    }
    else if (fourcc == "MOCV") {
      size_t spos = gf.getPos();
      //			Vertex colors, 4 bytes per vertex (BGRA), for WMO groups using indoor lighting.
      //			I don't know if this is supposed to work together with, or replace, the lights referenced in MOLR. But it sure is the only way for the ground around the goblin smelting pot to turn red in the Deadmines. (but some corridors are, in turn, too dark - how the hell does lighting work anyway, are there lightmaps hidden somewhere?)
      //			- I'm pretty sure WoW does not use lightmaps in it's WMOs...
      //			After further inspection, this is it, actual pre-lit vertex colors for WMOs - vertex lighting is turned off. This is used if flag 0x2000 in the MOGI chunk is on for this group. This pretty much fixes indoor lighting in Ironforge and Undercity. The "light" lights are used only for M2 models (doodads and characters). (The "too dark" corridors seemed like that because I was looking at it in a window - in full screen it looks pretty much the same as in the game) Now THAT's progress!!!

      //gLog("CV: %d\n", size);
      //hascv = true;
      cv = (unsigned int*)gf.getPointer();

      // Temp, until we get this fully working.
      gf.seek(spos);
      VertexColors = new WMOVertColor[nVertices];
      gf.read(VertexColors, size);
      //			for (size_t x=0;x<nVertices;x++){
      //				WMOVertColor vc;
      //				gf.read(&vc,4);
      //				VertexColors.push_back(vc);
      //			}
    }
    else if (fourcc == "MLIQ") {
      // liquids
      WMOLiquidHeader hlq;
      gf.read(&hlq, sizeof(WMOLiquidHeader));
    }

    // TODO: figure out/use MFOG ?

    gf.seek(nextpos);
  }

  // ok, make a display list

  indoor = (flags & 8192) != 0;
  //gLog("Lighting: %s %X\n\n", indoor?"Indoor":"Outdoor", flags);

  initLighting(nLR, useLights);

  dl = glGenLists(1);
  glNewList(dl, GL_COMPILE);
  glDisable(GL_BLEND);

  glColor4f(1, 1, 1, 1);

  //	float xr=0,xg=0,xb=0;
  //	if (flags & 0x0040) xr = 1;
  //	if (flags & 0x2000) xg = 1;
  //	if (flags & 0x8000) xb = 1;
  //	glColor4f(xr,xg,xb,1);

  // assume that texturing is on, for unit 1

  IndiceToVerts = new uint32[nIndices];

  for (size_t b = 0; b<nBatches; b++) {
    WMOBatch *batch = &batches[b];
    WMOMaterial *mat = &wmo->mat[batch->texture];

    // build indice to vert array.
    for (size_t i = 0; i <= batch->indexCount; i++){
      size_t a = indices[batch->indexStart + i];
      for (size_t j = batch->vertexStart; j <= batch->vertexEnd; j++){
        if (vertices[a] == vertices[j]){
          IndiceToVerts[batch->indexStart + i] = j;
          break;
        }
      }
    }

    // setup texture
    glBindTexture(GL_TEXTURE_2D, mat->tex);

    bool atest = (mat->transparent) != 0;

    if (atest) {
      glEnable(GL_ALPHA_TEST);
      float aval = 0;
      if (mat->flags & 0x80) aval = 0.3f;
      if (mat->flags & 0x01) aval = 0.0f;
      glAlphaFunc(GL_GREATER, aval);
    }

    if (mat->flags & WMO_MATERIAL_CULL)
      glDisable(GL_CULL_FACE);
    else
      glEnable(GL_CULL_FACE);

    //		float fr,fg,fb;
    //		fr = rand()/(float)RAND_MAX;
    //		fg = rand()/(float)RAND_MAX;
    //		fb = rand()/(float)RAND_MAX;
    //		glColor4f(fr,fg,fb,1);

    bool overbright = ((mat->flags & 0x10) && !hascv);
    if (overbright) {
      // TODO: use emissive color from the WMO Material instead of 1,1,1,1
      GLfloat em[4] = { 1, 1, 1, 1 };
      glMaterialfv(GL_FRONT, GL_EMISSION, em);
    }

    // render
    glBegin(GL_TRIANGLES);
    for (size_t t = 0, i = batch->indexStart; t<batch->indexCount; t++, i++) {
      int a = indices[i];
      if (indoor && hascv) {
        setGLColor(cv[a]);
      }
      glNormal3f(normals[a].x, normals[a].z, -normals[a].y);
      glTexCoord2fv(texcoords[a]);
      glVertex3f(vertices[a].x, vertices[a].z, -vertices[a].y);
    }
    glEnd();

    if (overbright) {
      GLfloat em[4] = { 0, 0, 0, 1 };
      glMaterialfv(GL_FRONT, GL_EMISSION, em);
    }

    if (atest) {
      glDisable(GL_ALPHA_TEST);
    }
  }

  glColor4f(1, 1, 1, 1);
  glEnable(GL_CULL_FACE);

  glEndList();

  gf.close();

  // hmm
  indoor = false;

  ok = true;
}


void WMOGroup::initLighting(int nLR, short *useLights)
{
  dl_light = 0;
  // "real" lighting?
  if ((flags & 0x2000) && hascv) {

    Vec3D dirmin(1, 1, 1);
    float lenmin;
    int lmin;

    for (size_t i = 0; i<nDoodads; i++) {
      lenmin = 999999.0f*999999.0f;
      lmin = 0;
      WMOModelInstance &mi = wmo->modelis[ddr[i]];
      for (size_t j = 0; j<wmo->nLights; j++) {
        WMOLight &l = wmo->lights[j];
        Vec3D dir = l.pos - mi.pos;
        float ll = dir.lengthSquared();
        if (ll < lenmin) {
          lenmin = ll;
          dirmin = dir;
          lmin = (int)j;
        }
      }
      mi.light = lmin;
      mi.ldir = dirmin;
    }

    outdoorLights = false;
  }
  else {
    outdoorLights = true;
  }
}

void WMOGroup::draw()
{
  if (!ok) {
    visible = false;
    return;
  }

  visible = true;

  if (hascv) {
    glDisable(GL_LIGHTING);
  }
  //setupFog();

  glCallList(dl);

  if (hascv) {
    glEnable(GL_LIGHTING);
  }
}

void WMOGroup::drawDoodads(int doodadset)
{
  if (!visible) return;
  if (nDoodads == 0) return;
  if (doodadset<0) return;

  //setupFog();

  /*
  float xr=0,xg=0,xb=0;
  if (flags & 0x0040) xr = 1;
  //if (flags & 0x0008) xg = 1;
  if (flags & 0x8000) xb = 1;
  glColor4f(xr,xg,xb,1);
  */

  // draw doodads
  glColor4f(1, 1, 1, 1);
  for (size_t i = 0; i<nDoodads; i++) {
    short dd = ddr[i];

    bool inSet;
    if (doodadset == -1) {
      inSet = false;
    }
    else {
      inSet = (((dd >= wmo->doodadsets[doodadset].start) && (dd < (wmo->doodadsets[doodadset].start + (int)wmo->doodadsets[doodadset].size)))
               || (wmo->includeDefaultDoodads && (dd >= wmo->doodadsets[0].start) && ((dd < (wmo->doodadsets[0].start + (int)wmo->doodadsets[0].size)))));
    }

    if (inSet) {

      WMOModelInstance &mi = wmo->modelis[dd];

      if (!outdoorLights) {
        glDisable(GL_LIGHT0);
        WMOLight::setupOnce(GL_LIGHT2, mi.ldir, mi.lcol);
      }
      else {
        glEnable(GL_LIGHT0);
      }

      wmo->modelis[dd].draw();
    }
  }

  glDisable(GL_LIGHT2);

  glColor4f(1, 1, 1, 1);

}

void WMOGroup::drawLiquid()
{
  if (!visible)
    return;

  /*
  // draw liquid
  // TODO: culling for liquid boundingbox or something
  if (lq) {
  setupFog();
  if (outdoorLights) {
  //gWorld->outdoorLights(true);
  } else {
  // TODO: setup some kind of indoor lighting... ?
  //gWorld->outdoorLights(false);
  glEnable(GL_LIGHT2);
  glLightfv(GL_LIGHT2, GL_AMBIENT, Vec4D(0.1f,0.1f,0.1f,1));
  glLightfv(GL_LIGHT2, GL_DIFFUSE, Vec4D(0.8f,0.8f,0.8f,1));
  glLightfv(GL_LIGHT2, GL_POSITION, Vec4D(0,1,0,0));
  }
  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glDepthMask(GL_TRUE);
  glColor4f(1,1,1,1);
  lq->draw();
  glDisable(GL_LIGHT2);
  }
  */
}

void WMOGroup::setupFog()
{
  /*
  if (outdoorLights || fog==-1) {
  gWorld->setupFog();
  } else {
  wmo->fogs[fog].setup();
  }
  */
}

WMOGroup::WMOGroup() :
dl(0), ddr(0), vertices(NULL), normals(NULL), texcoords(NULL),
indices(NULL), materials(NULL), nTriangles(0), nVertices(0),
nIndices(0), nBatches(0)
{
}

WMOGroup::~WMOGroup()
{
  cleanup();
  delete ddr;
}

void WMOGroup::cleanup()
{
  if (dl) glDeleteLists(dl, 1);
  dl = 0;
  if (dl_light) glDeleteLists(dl_light, 1);
  dl_light = 0;
  ok = false;

  delete vertices;
  vertices = 0;
  delete normals;
  normals = 0;
  delete texcoords;
  texcoords = 0;
  delete indices;
  indices = 0;
  delete materials;
  materials = 0;
  delete batches;
  batches = 0;
}

void WMOGroup::updateModels(bool load)
{
  if (!ddr || !ok || nDoodads == 0)
    return;

  for (size_t i = 0; i<nDoodads; i++) {
    short dd = ddr[i];

    bool inSet;

    if (wmo->doodadset == -1) {
      inSet = false;
    }
    else {
      inSet = (((dd >= wmo->doodadsets[wmo->doodadset].start) && (dd < (wmo->doodadsets[wmo->doodadset].start + (int)wmo->doodadsets[wmo->doodadset].size)))
               || (wmo->includeDefaultDoodads && (dd >= wmo->doodadsets[0].start) && ((dd < (wmo->doodadsets[0].start + (int)wmo->doodadsets[0].size)))));
    }

    if (inSet) {
      WMOModelInstance &mi = wmo->modelis[dd];

      if (load && !mi.model)
        mi.loadModel(wmo->loadedModels);
      else if (!load && mi.model)
        mi.unloadModel(wmo->loadedModels);
    }
  }
}

void WMOGroup::init(WMO *wmo, GameFile &f, int num, char *names)
{
  /*
  Groups don't have placement or orientation information, because the coordinates for the
  vertices in the additional .WMO files are already correctly transformed relative to (0,0,0)
  which is the entire WMO's base position in model space.
  The name offsets seem to be incorrect (or something else entirely?). The correct name
  offsets are in the WMO group file headers. (along with more descriptive names for some groups)
  It is just the index. You have to find the offsets by yourself. --Schlumpf 10:17, 31 July 2007 (CEST)
  The flags for the groups seem to specify whether it is indoors/outdoors, probably to
  choose what kind of lighting to use. Not fully understood. "Indoors" and "Outdoors" are flags
  used to tell the client whether certain spells can be cast and abilities used. (Example: Entangling
  Roots cannot be used indoors).
  */

  this->wmo = wmo;
  this->num = num;

  // extract group info from f
  f.read(&flags, 4);
  float ff[3];
  f.read(ff, 12); // Bounding box corner 1
  v1 = Vec3D(ff[0], ff[1], ff[2]);
  f.read(ff, 12); // Bounding box corner 2
  v2 = Vec3D(ff[0], ff[1], ff[2]);
  int nameOfs;
  f.read(&nameOfs, 4); // name in MOGN chunk (or -1 for no name?)

  // TODO: get proper name from group header and/or dbc?
  /*
  if (nameOfs > 0) {
  name = string(names + nameOfs);
  } else name = "(no name)";
  */

  ddr = 0;
  nDoodads = 0;

  //lq = 0;

  ok = false;
  visible = false;
}
