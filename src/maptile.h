#ifndef MAPTILE_H
#define MAPTILE_H

#define TILESIZE (533.33333f)
#define CHUNKSIZE ((TILESIZE) / 16.0f)
#define UNITSIZE (CHUNKSIZE / 8.0f)
#define ZEROPOINT (32.0f * (TILESIZE))

#define	CHUNKS_IN_TILE	16

#include "video.h"
#include "mpq.h"
#include "wmo.h"
#include "WoWModel.h"
#include "liquid.h"
#include <vector>
#include <string>

class MapTile;
class MapChunk;

class World;

typedef unsigned char      uint8;
typedef unsigned short     uint16;
typedef unsigned int       uint32;

struct MapChunkHeader {
	uint32 flags;
	uint32 ix;
	uint32 iy;
	uint32 nLayers;
	uint32 nDoodadRefs;
	uint32 ofsHeight; // MCVT
	uint32 ofsNormal; // MCNR
	uint32 ofsLayer; // MCLY
	uint32 ofsRefs; // MCRF
	uint32 ofsAlpha; // MCAL
	uint32 sizeAlpha;
	uint32 ofsShadow; // MCSH
	uint32 sizeShadow;
	uint32 areaid;
	uint32 nMapObjRefs;
	uint32 holes;
	uint16 s1; // UINT2[8][8] ReallyLowQualityTextureingMap;	// the content is the layer being on top, I guess.
	uint16 s2;
	uint32 d1;
	uint32 d2;
	uint32 d3;
	uint32 predTex;
	uint32 nEffectDoodad;
	uint32 ofsSndEmitters; // MCSE
	uint32 nSndEmitters; // will be set to 0 in the client if ofsSndEmitters doesn't point to MCSE!
	uint32 ofsLiquid; // MCLQ
	uint32 sizeLiquid; // 8 when not used; only read if >8.
	float  zpos;
	float  xpos;
	float  ypos;
	uint32 textureId; // MCCV, only with flags&0x40, had UINT32 textureId; in ObscuR's structure.
	uint32 props;
	uint32 effectId;
};

struct SWaterLayer
{
	uint16 flags;
	uint16 type;
	float levels[2];
	uint8 x; 
	uint8 y; 
	uint8 w; 
	uint8 h; 
	
	bool hasmask;
	unsigned char mask[8]; // not used

	std::vector<float> heights;
	std::vector<uint8> alphas;
	std::vector<bool> renderTiles;
};

struct SWaterTile
{
	std::vector< SWaterLayer > layers;
	uint8 quadmask[16];  // not used
};

const int mapbufsize = 9*9 + 8*8;

class MapNode {
public:

	MapNode(int x, int y, int s)
		:px(x), py(y), size(s), vmin(0), vmax(0), vcenter(0), mt(0)
	{
		for(int i=0; i<4; i++)
			children[i] = 0;
	}

	virtual ~MapNode() {}

	int px, py, size;

	Vec3D vmin, vmax, vcenter;

	MapNode *children[4];
	MapTile *mt;

	virtual void draw();
	void setup(MapTile *t);
	void cleanup();

};

class MapChunk : public MapNode {
public:
	size_t nTextures;

	float xbase, ybase, zbase;
	float r;
	bool mBigAlpha;
	MapChunkHeader header;

	int areaID;

	std::vector<GLuint> wTextures;
	void initTextures(wxString basename, int first, int last);

	bool haswater;
	std::vector< SWaterLayer > waterLayer;
	bool visible;
	bool hasholes;
	float waterlevel[2];

	TextureID textures[4];
	TextureID alphamaps[3];
	TextureID shadow, blend;

	int animated[4];

	GLuint vertices, normals;

	short *strip;
	int striplen;

	Liquid *lq;

	MapChunk():MapNode(0,0,0),nTextures(0),xbase(0),ybase(0),zbase(0),r(0),areaID(-1),
		haswater(false),visible(false),hasholes(false),shadow(0),blend(0),
		vertices(0),normals(0),strip(0),striplen(0),lq(0)
	{
		waterlevel[0] = 0;
		waterlevel[1] = 0;
		for(int i=0; i<4; i++) {
			textures[i] = 0;
		}
		for(int i=0; i<3; i++) {
			alphamaps[i] = 0;
		}
	}
	
	void init(MapTile* mt, MPQFile &f, bool bigAlpha);
	void destroy();
	void initStrip(int holes);

	void draw();
	void drawNoDetail();
	void drawPass(int anim);
	void drawWater();

	MapTile* maptile;
	Vec3D tn[mapbufsize], tv[mapbufsize];
};

const size_t stripsize2 = 16*18 + 7*2 + 8*2;

class MapTile: public Displayable {
public:
	wxArrayString textures;
	wxArrayString wmos;
	wxArrayString models;

	//std::vector<WMOInstance> wmois;
	//std::vector<ModelInstance> modelis;
	size_t nWMO;
	size_t nMDX;

	wxString name;
	int x, z;
	bool ok;
	bool mBigAlpha;

	Vec3D viewpos;
	Vec3D viewrot;

	//World *world;

	float xbase, zbase;

	MapChunk chunks[16][16];

	MapNode topnode;

	MapTile(wxString filename);
	~MapTile();

	void draw();
	void drawWater();
	void drawObjects();
	void drawSky();
	//void drawPortals();
	void drawModels();

	/// Get chunk for sub offset x,z
	MapChunk *getChunk(unsigned int x, unsigned int z);

	void initDisplay();
	short mapstrip2[stripsize2];
};

int indexMapBuf(int x, int y);


// 8x8x2 version with triangle strips, size = 8*18 + 7*2
const int stripsize = 8*18 + 7*2;
template <class V>
void stripify(V *in, V *out)
{
	for (ssize_t row=0; row<8; row++) {
		V *thisrow = &in[indexMapBuf(0,row*2)];
		V *nextrow = &in[indexMapBuf(0,(row+1)*2)];

		if (row>0) *out++ = thisrow[0];
		for (ssize_t col=0; col<9; col++) {
			*out++ = thisrow[col];
			*out++ = nextrow[col];
		}
		if (row<7) *out++ = nextrow[8];
	}
}

// high res version, size = 16*18 + 7*2 + 8*2

template <class V>
void stripify2(V *in, V *out)
{
	for (ssize_t row=0; row<8; row++) { 
		V *thisrow = &in[indexMapBuf(0,row*2)];
		V *nextrow = &in[indexMapBuf(0,row*2+1)];
		V *overrow = &in[indexMapBuf(0,(row+1)*2)];

		if (row>0) *out++ = thisrow[0];// jump end
		for (ssize_t col=0; col<8; col++) {
			*out++ = thisrow[col];
			*out++ = nextrow[col];
		}
		*out++ = thisrow[8];
		*out++ = overrow[8];
		*out++ = overrow[8];// jump start
		*out++ = thisrow[0];// jump end
		*out++ = thisrow[0];
		for (ssize_t col=0; col<8; col++) {
			*out++ = overrow[col];
			*out++ = nextrow[col];
		}
		if (row<8) *out++ = overrow[8];
		if (row<7) *out++ = overrow[8];// jump start
	}
}


#endif
