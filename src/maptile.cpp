#include "modelviewer.h"
#include "globalvars.h"
#include "maptile.h"
//#include "world.h"
#include "vec3d.h"
//#include "video.h"
#include "shaders.h"
#include <cassert>
#include <algorithm>
using namespace std;

struct WaterTile
{
	/*
	uint32 ofsLayer;
	This is an Offset to the first Water Layer, it has tobe an offset because there can be multiple layers.
	*/
	uint32 ofsLayer; 

	/*
	uint32 layerCount; 
	This is the count of the water layers present for this tile
	if layerCount is 0 there is no water in this tile also ofsVisibilityMask is 0
	because that information is also not needed.
	*/
	uint32 layerCount; 

	/*
	uint32 ofsVisibilityMask;
	This field requires some explanation.
	Its an offset to a uint8[8] data block.
	the data block contains a bit field 8x8 = 64 bit every bit represents a quad between 4 verts. 
	But the bits in this field are only set to 1 if the field is really visible this is not ment in a
	technical way. To make it clear all of the 4 vertices have an alpha value (i explain them later)
	if only one of these alpha values smaller then 10 the bit is not set

	if the first byte is 0x17h which is in binary 00010111b it has to look like this in a grid:

	0123...
	1+-----> bits
	2| 11101000
	3|
	.|
	.V
	bytes

	*/
	uint32 ofsVisibilityMask; 
};

struct WaterLayer
{
	/*
	uint16 flags;
	known values:
	2 
	-> Means no min and max values
	-> type == 2 ?
	-> ocean

	5 
	-> Means min and max values are given
	-> type == 0 ?
	-> waterfalls, rivers, lakes (everything that is not on ocean level)

	[TODO] Find all other values
	*/
	uint16 flags;

	/*
	uint16 type;
	The type seems tobee depend on the flags ??
	Maybe its an uint32 and in general -> flags

	[TODO] Find all other values
	*/
	uint16 type;

	/*
	float min;
	The minimum height from all vertices if flags == 5
	*/
	float min;

	/*
	float max;
	The maximum height from all vertices if flags == 5
	*/
	float max;

	/*
	uint8 x, y, w, h;
	These 4 values are used to define the size of the sub mask and
	the count of height and alpha values needed.

	x and y can be 0-7
	w and h can be 1-8

	Lets say we have a VisibilityMask like this:	
	00000000
	00000000
	00000000
	00000000
	00111000
	11111100
	11111111
	11111111

	And the given Values for x y w h are 0 4 8 4 it means there MAYBE more info for this part (marked with X):
	00000000
	00000000
	xy 00000000
	\00000000_
	XXXXXXXX|
	XXXXXXXX| h
	XXXXXXXX|
	XXXXXXXX_
	|---w--|
	*/
	uint8 x, y, w, h;

	/*
	uint32 ofsDisplayMask;
	This is a more detailed version of the VisibilityMask. Its an uint8[w*h/8] data block.

	The grid is created as before but every line only contains w bits

	If the offset is 0 then every quad marked by x y w h is displayed
	*/
	uint32 ofsDisplayMask;


	/*
	uint32 ofsHeigthAlpha;

	This offset points to an array of heights and after that there is an array of alpha values.

	the size of both arrays is (w+1)*(h+1)
	8*8 quads -> 9*9 vertices so if w or h is 8 we need 9 values this explains the "+1"

	the heights array float[(w+1)*(h+1)] is only present if the flags == 5 otherwise (2) its not required

	the alpha array uint8[(w+1)*(h+1)] seems tobe always present if the offset is given and comes always
	after the heights array and if heights are not given it is directly at ofsHeigthAlpha
	*/
	uint32 ofsHeigthAlpha;
};

void fprintbu8( FILE* file, uint8 value )
{
	bool b[8];
	unsigned int x = 1;
	for( unsigned i = 0; i < 8; i++ )
	{
		b[i] = (value & x) ? true : false;
		x <<= 1;
	}

	for( unsigned i = 0 ; i < 8; i++ )
	{
		if( b[i] )
			fprintf( file, "1" );
		else
			fprintf( file, "0" );
	}
}

bool getBitH2L( unsigned char* data, unsigned bit )
{
	unsigned char byte = data[bit / 8];
	unsigned char pos = bit % 8;

	unsigned char mask = 0x80;
	mask >>= pos;

	return (byte & mask) == mask;
}

bool getBitL2H( unsigned char* data, unsigned bit )
{
	unsigned char byte = data[bit / 8];
	unsigned char pos = bit % 8;

	unsigned char mask = 0x1;
	mask <<= pos;

	return (byte & mask) == mask;
}

struct MapTileHeader {
	uint32 flags;
	uint32 MCIN;
	uint32 MTEX;
	uint32 MMDX;
	uint32 MMID;
	uint32 MWMO;
	uint32 MWID;
	uint32 MDDF;
	uint32 MODF;
	uint32 MFBO;
	uint32 MH2O;
	uint32 MTFX;
	uint32 pad4;
	uint32 pad5;
	uint32 pad6;
	uint32 pad7;
};

struct MH2O_Header {
	uint32 ofsInformation; // An offset to the first data-block.
	uint32 layers; // 0 if the chunk has no liquids. If > 1, the offsets will point to arrays.
	uint32 ofsData; // An offset to the second data-block.
};

struct MH2O_Information {
	uint16 LiquidType; // Points to LiquidType.dbc
	uint16 flags;
	float levels[2];
	uint8 x; // The X offset of the liquid square (0-7)
	uint8 y; // The Y offset of the liquid square (0-7)
	uint8 w; // The width of the liquid square (1-8)
	uint8 h; // The height of the liquid square (1-8)
	uint32 ofsMask2;		// points to an array of bits with information about the mask. w*h bits (=h bytes).
	uint32 ofsHeightmap; // Another offset to data.
};

int gdetailtexcoords, galphatexcoords;

void initGlobalVBOs()
{
	if (gdetailtexcoords==0 && galphatexcoords==0) {

		GLuint detailtexcoords, alphatexcoords;

		Vec2D temp[mapbufsize], *vt;
		float tx,ty;
		
		// init texture coordinates for detail map:
		vt = temp;
		int detail_size = 1;
		const float detail_half = 0.5f * detail_size / 8.0f;
		for (ssize_t j=0; j<17; j++) {
			for (size_t i=0; i<((j%2)?8:9); i++) {
				tx = detail_size / 8.0f * i;
				ty = detail_size / 8.0f * j * 0.5f;
				if (j%2) {
					// offset by half
					tx += detail_half;
				}
				*vt++ = Vec2D(tx, ty);
			}
		}

		glGenBuffersARB(1, &detailtexcoords);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, detailtexcoords);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*2*sizeof(float), temp, GL_STATIC_DRAW_ARB);

		// init texture coordinates for alpha map:
		vt = temp;
		const float alpha_half = 0.5f * 1.0f / 8.0f;
		for (ssize_t j=0; j<17; j++) {
			for (size_t i=0; i<((j%2)?8:9); i++) {
				tx = 1.0f / 8.0f * i;
				ty = 1.0f / 8.0f * j * 0.5f;
				if (j%2) {
					// offset by half
					tx += alpha_half;
				}
				//*vt++ = Vec2D(tx*0.95f, ty*0.95f);
				const int divs = 32;
				const float inv = 1.0f / divs;
				const float mul = (divs-1.0f);
				*vt++ = Vec2D(tx*(mul*inv), ty*(mul*inv));
			}
		}

		glGenBuffersARB(1, &alphatexcoords);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, alphatexcoords);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*2*sizeof(float), temp, GL_STATIC_DRAW_ARB);

		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);


		gdetailtexcoords=detailtexcoords;
		galphatexcoords=alphatexcoords;
	}

}

void MapTile::initDisplay()
{
	// default strip indices
	short *defstrip = new short[stripsize2];
	for (size_t i=0; i<stripsize2; i++) 
		defstrip[i] = (short)i; // note: this is ugly and should be handled in stripify
	//mapstrip2 = new short[stripsize2];
	stripify2<short>(defstrip, mapstrip2);
	delete[] defstrip;
	
	initGlobalVBOs();
}

/*
MapTile is ADT
http://madx.dk/wowdev/wiki/index.php?title=ADT
*/
MapTile::MapTile(wxString filename): nWMO(0), nMDX(0), topnode(0,0,16)
{
	x = atoi((char *)filename.Mid(filename.Len()-9, 2).c_str());
	z = atoi((char *)filename.Mid(filename.Len()-6, 2).c_str());
	xbase = x * TILESIZE;
	zbase = z * TILESIZE;
	// TODO: get bigAlpha from world
	// mBigAlpha=bigAlpha;
	viewpos.x = 14937.999f+200.0f;
	viewpos.y = -260.0f+200.0f;
	viewpos.z = 18400.0f;
	viewrot.x = 0;
	viewrot.y = 0;
	viewrot.z = 0;


	wxLogMessage(wxT("Loading tile %s"),filename.c_str());
	initDisplay();

	 // [FLOW] DON'T REMOVE i use this file extraction method to debug the adt format
/*
	FILE* exFile = fopen( std::string(filename).substr( std::string(filename).find_last_of( "\\" ) +1 ).c_str(), "wb" );
	if( exFile )
	{
		MPQFile ex(filename);

		fwrite( ex.getBuffer(), ex.getSize( filename ),1, exFile );

		ex.close();
		fclose( exFile );
	}
*/

	MPQFile f(filename);
	ok = !f.isEof();
	if (!ok) {
		wxLogMessage(wxT("Error: loading %s"),filename.c_str());
		return;
	}

	name = filename;
	char fourcc[5];
	uint32 size;

	size_t mcnk_offsets[CHUNKS_IN_TILE*CHUNKS_IN_TILE], mcnk_sizes[CHUNKS_IN_TILE*CHUNKS_IN_TILE];
	memset(mcnk_offsets, 0, sizeof(mcnk_offsets));
	memset(mcnk_sizes, 0, sizeof(mcnk_sizes));

	while (!f.isEof()) {
		memset(fourcc, 0, 4);
		size = 0;
		f.read(fourcc,4);
		f.read(&size, 4);

		flipcc(fourcc);
		fourcc[4] = 0;

		if (size == 0)
			continue;

		// remember the next absolute pos
		size_t nextpos = f.getPos() + size;

		if (strncmp(fourcc, "MVER", 4) == 0) {
		}
		else if (strncmp(fourcc, "MHDR", 4) == 0) {
		}
		else if (strncmp(fourcc,"MCIN",4)==0) {
			/*
			Index for MCNK chunks. Contains 256 records of 16 bytes, which have the following format:
			struct SMChunkInfo // 03-29-2005 By ObscuR
			{
			000h  MCNK* mcnk;				// absolute offset.
			004h  UINT32 size;				// the size of the MCNK chunk, this is refering to.
			008h  UINT32 Unused_flags;			// these two are always 0. only set in the client.
			00Ch  UINT32 Unused_asyncId;
			010h
			};
			*/
			// mapchunk offsets/sizes
			if (size == CHUNKS_IN_TILE*CHUNKS_IN_TILE*16) {
				for (size_t i=0; i<CHUNKS_IN_TILE*CHUNKS_IN_TILE; i++) {
					f.read(&mcnk_offsets[i], 4);
					f.read(&mcnk_sizes[i], 4);
					f.seekRelative(8);
				}
			} else
				wxLogMessage(wxT("Error: wrong MCIN chunk %d."), size);
		}
		else if (strncmp(fourcc,"MTEX",4)==0) {
			/*
			List of textures used by the terrain in this map tile.
			A contiguous block of zero-terminated strings, that are complete filenames with paths. The textures will later be identified by their position in this list.
			*/
			// texture lists
			char *buf = new char[size+1];
			f.read(buf, size);
			buf[size] = 0;
			char *p=buf;
			//int t=0;
			while (p<buf+size) {
				wxString texpath(p, wxConvUTF8);
				p+=strlen(p)+1;
				fixname(texpath);

				if (video.supportShaders) {
					wxString texshader = texpath;
					// load the specular texture instead
					texshader.insert(texshader.length()-4,wxT("_s"));
					if (MPQFile::exists(texshader))
						texpath = texshader;
				}

				texturemanager.add(texpath);
				textures.push_back(texpath);
			}
			delete[] buf;
		}
		else if (strncmp(fourcc,"MMDX",4)==0) {
			/*
			List of filenames for M2 models that appear in this map tile. A contiguous block of zero-terminated strings.
			*/
			// models ...
			// MMID would be relative offsets for MMDX filenames
			/*
			// TODO
			char *buf = new char[size+1];
			f.read(buf, size);
			buf[size] = 0;
			char *p=buf;
			int t=0;
			while (p<buf+size) {
				std::string path(p);
				p+=strlen(p)+1;
				fixname(path);

				gWorld->modelmanager.add(path);
				models.push_back(path);
			}
			delete[] buf;
			*/
		}
		else if (strncmp(fourcc,"MMID",4)==0) {
			/*
			Lists the relative offsets of string beginnings in the above MMDX chunk. One 32-bit integer per offset.
			This will be referenced in the offsets in MDDF --Cromon 16:38, 28 August 2009 (CEST)
			*/
		}
		else if (strncmp(fourcc,"MWMO",4)==0) {
			/*
			List of filenames for WMOs (world map objects) that appear in this map tile. A contiguous block of zero-terminated strings.
			*/
			// map objects
			// MWID would be relative offsets for MWMO filenames
			/*
			// TODO
			char *buf = new char[size+1];
			f.read(buf, size);
			buf[size] = 0;
			char *p=buf;
			while (p<buf+size) {
				std::string path(p);
				p+=strlen(p)+1;
				fixname(path);

				gWorld->wmomanager.add(path);
				wmos.push_back(path);
			}
			delete[] buf;
			*/
		}
		else if (strncmp(fourcc,"MWID",4)==0) {
			/*
			Lists the relative offsets of string beginnings in the above MWMO chunk. One 32-bit integer per offset.
			Again referenced in MODF
			*/
		}
		else if (strncmp(fourcc,"MDDF",4)==0) {
			/*
			Placement information for doodads (M2 models). 36 bytes per model instance.
			Offset 	Type 		Description
			0x00 	uint32 		ID (index in the MMID list)
			0x04 	uint32 		unique identifier for this instance
			0x08 	Vec3F 		Position (X,Y,Z)
			0x14 	Vec3F 		Orientation (A,B,C)
			0x20 	uint16 		scale factor * 1024 (it's scale / 1024 for the other way around)
			0x22 	uint16 		flags, known: &1 (sets the internal flags to 0x801 instead of 0x1. (WDOODADDEF.var0xC))
			struct SMDoodadDef // 03-31-2005 By ObscuR
			{
			000h  UINT32 nameId;
			004h  UINT32 uniqueId;		
			008h  float pos[3];		
			00Ch 
			010h 
			014h  float rot[3];		
			018h  		
			01Ch  		
			020h  UINT16 scale;	
			022h  UINT16 flags;
			024h  
			};
			Flags:
			&1 is set for biodomes in netherstorm. 
			&2 is set for some clovers and shrubbery in northrend.
			Both are only used in Expansion01 respective Northrend.
			The instance information specifies the actual M2 model to use, its absolute position and orientation within the world. The orientation is defined by rotations (in degrees) about the 3 axes as such (this order of operations is for OpenGL, so the transformations "actually" happen in reverse):
			Rotate around the Y axis by B-90
			Rotate around the Z axis by -A
			Rotate around the X axis by C
			If you can't get those working, try these (The other ones didn't work for me, these does. My coordinate system is equal to WoW's (X is depth and Z is up))
			Rotate around Z (up) by B + 180 (or minus if you will)
			Rotate around X (depth) by +C
			Rotate around Y by +A
			-MaiN
			*/
			// model instance data
			/*
			// TODO
			nMDX = (int)size / 36;
			for (size_t i=0; i<nMDX; i++) {
				int id;
				f.read(&id, 4);
				Model *model = (Model*)gWorld->modelmanager.items[gWorld->modelmanager.get(models[id])];
				ModelInstance inst(model, f);
				modelis.push_back(inst);
			}
			*/
		}
		else if (strncmp(fourcc,"MODF",4)==0) {
			/*
			Placement information for WMOs. 64 bytes per WMO instance.
			Offset 	Type 		Description
			0x00 	uint32 		ID (index in the MWID list)
			0x04 	uint32 		unique identifier for this instance
			0x08 	3 floats 	Position (X,Y,Z)
			0x14 	3 floats 	Orientation (A,B,C)
			0x20 	3 floats 	Upper Extents
			0x2C 	3 floats 	Lower Extents
			0x38 	uint16 		flags
			0x3A 	uint16 		Doodad set index
			0x3C 	uint16 		name set?
			0x3E 	uint16 		unknown
			(old: 0x3C 	uint32 		Name set); it reads only a WORD into the WMAPOBJDEF structure. I don't know about the rest. Oo
			To the flags: known: &1 (does something with the extends, no idea what exactely: paste, is set eg for World\wmo\Azeroth\Buildings\GuardTower\GuardTower.wmo and World\wmo\Azeroth\Buildings\GuardTower\GuardTower_destroyed.wmo in in DeathknightStart_43_27. Maybe used for Phasing?)
			struct SMMapObjDef // 03-29-2005 By ObscuR
			{
			000h  UINT32 nameId;		
			004h  UINT32 uniqueId;		
			008h  float pos[3];
			00Ch  		
			010h  		
			014h  float rot[3];
			018h  	
			01Ch  		
			020h  float extents[6];
			024h  	 
			028h   	
			02Ch 	
			030h 		
			034h  		
			038h  UINT32 flags;		
			03Ch  UINT16 doodadSet;
			03Eh  UINT16 nameSet;
			040h 
			}; 
			The positioning and orientation is done the same way as in the MDDF chunk. There is no scaling. Two additional positions and two integers are also given. They might or might not be used for lighting...?
			The unique identifier is important for WMOs, because multiple map tiles might want to draw the same WMO. This identifier is used to ensure that each specific instance can only be drawn once. (a unique identifier is required because the model name is not usable for this purpose, since it is possible to have more than one instance of the same WMO, like some bridges in Darkshore)
			*/
			// wmo instance data
			/* 
			// TODO
			nWMO = (int)size / 64;
			for (size_t i=0; i<nWMO; i++) {
				int id;
				f.read(&id, 4);
				WMO *wmo = (WMO*)gWorld->wmomanager.items[gWorld->wmomanager.get(wmos[id])];
				WMOInstance inst(wmo, f);
				wmois.push_back(inst);
			}
			*/
		}
		else if (strncmp(fourcc,"MH2O",4)==0) {
			unsigned char *abuf = f.getPointer();
			struct WaterTile *mh2oh;
			struct WaterLayer *mh2oi;

			for(size_t i=0; i<CHUNKS_IN_TILE*CHUNKS_IN_TILE;i++)
			{ // 256*12=3072 bytes
				//SWaterTile waterTile;

				mh2oh = (struct WaterTile *)abuf;
				//
				// start at 3072, 3072+24, 3072+24*2, ....
				printf( "%d MH2O: %X, %X %d %X", i,
					abuf - f.getPointer(),
					0x86fa + mh2oh->ofsLayer,
					mh2oh->layerCount,
					0x86fa + mh2oh->ofsVisibilityMask);

				for( unsigned j = 0; j < mh2oh->layerCount; j++ )
				{
					mh2oi = (struct WaterLayer *) (f.getPointer()+mh2oh->ofsLayer + sizeof( WaterLayer ) * j );
					SWaterLayer waterLayer;
					//memcpy( waterTile.quadmask, f.getPointer()+mh2oh->ofsVisibilityMask, 16 );

					waterLayer.x = mh2oi->x;
					waterLayer.y = mh2oi->y;
					waterLayer.w = mh2oi->w;
					waterLayer.h = mh2oi->h;
					waterLayer.flags = mh2oi->flags;
					waterLayer.levels[0] = mh2oi->min;
					waterLayer.levels[1] = mh2oi->max;
					waterLayer.type = mh2oi->type;


					// [FLOW] this step is required otherwise the print function interprets the data wrong
					unsigned int x = mh2oi->x;
					unsigned int y = mh2oi->y;
					unsigned int w = mh2oi->w;
					unsigned int h = mh2oi->h;

					/*if( x >= 256 || y >= 256 || w >= 256 || h >= 256 )
					{
					while( false );
					}*/

					printf( " Layer %d: %d %d %f-%f %d-%d-%d-%d %X-%X", j,
						mh2oi->flags,
						mh2oi->type, 
						mh2oi->min,
						mh2oi->max,
						x,
						y,
						w, 
						h,
						0x86fa + mh2oi->ofsDisplayMask, 
						0x86fa + mh2oi->ofsHeigthAlpha);

					waterLayer.hasmask = mh2oi->ofsDisplayMask != 0;
					if( waterLayer.hasmask )
					{
						unsigned co = w * h / 8;
						if( w * h % 8 != 0 )
							co++;
						memcpy( waterLayer.mask, f.getPointer() + mh2oi->ofsDisplayMask, co );

						for(size_t j = 0; j < waterLayer.w * waterLayer.h; j++ )
						{
							if( getBitL2H( waterLayer.mask, (unsigned int)j ) )		
							{
								waterLayer.renderTiles.push_back( true );
							}
							else
							{
								waterLayer.renderTiles.push_back( false );
							}
						}
					}

					if( mh2oi->ofsHeigthAlpha != 0 && mh2oi->flags == 2 && mh2oi->type == 2 )
					{
						unsigned char* pUnknowns = (unsigned char*)f.getPointer()+mh2oi->ofsHeigthAlpha;
						for( int g = 0; g < (mh2oi->w + 1) * (mh2oi->h + 1); g++ )
						{
							waterLayer.alphas.push_back( pUnknowns[g] );
						}
					}
					else if( mh2oi->ofsHeigthAlpha != 0 && mh2oi->flags == 5 && mh2oi->type == 0 )
					{
						float* pHeights = (float*)(f.getPointer()+mh2oi->ofsHeigthAlpha);
						unsigned char* pUnknowns = (unsigned char*)f.getPointer()+mh2oi->ofsHeigthAlpha + sizeof( float ) * (mh2oi->w + 1) * (mh2oi->h + 1);
						for( int g = 0; g < (mh2oi->w + 1) * (mh2oi->h + 1); g++ )
						{
							waterLayer.heights.push_back( pHeights[g] );
							waterLayer.alphas.push_back( pUnknowns[g] );
						}
					}
					else if( mh2oi->ofsHeigthAlpha != 0 && mh2oi->flags == 7 && mh2oi->type == 1 )
					{
						float* pHeights = (float*)(f.getPointer()+mh2oi->ofsHeigthAlpha);
						//unsigned char* pUnknowns = (unsigned char*)f.getPointer()+mh2oi->ofsHeigthAlpha + sizeof( float ) * (mh2oi->w + 1) * (mh2oi->h + 1);
						for( int g = 0; g < (mh2oi->w + 1) * (mh2oi->h + 1); g++ )
						{
							waterLayer.heights.push_back( pHeights[g] );
							//waterLayer.alphas.push_back( pUnknowns[g] );
						}
					}
					else if( mh2oi->ofsHeigthAlpha != 0 )
					{
						wxLogMessage(wxT("Unknown flag combination: %s."), filename.c_str());
					}

					chunks[i/CHUNKS_IN_TILE][i%CHUNKS_IN_TILE].waterLayer.push_back( waterLayer );
				}
				printf( "\n");
				abuf += sizeof(struct WaterTile);

				//Water.push_back( waterTile );
			}
		}
		else if(strncmp(fourcc, "MCNK", 4) == 0) {
			// MCNK data will be processed separately ^_^
		}
		else if(strncmp(fourcc, "MFBO", 4) == 0) {
			/*
			A bounding box for flying.
			This chunk is a "box" defining, where you can fly and where you can't. It also defines the height at which one you will fall into nowhere while your camera remains at the same position. Its actually two planes with 3*3 coordinates per plane.
			Therefore the structure is:
			struct plane{
			short[3][3] height;
			};
			struct MFBO
			{
			plane maximum;
			plane minimum;
			};
			*/
		}
		else if(strncmp(fourcc, "MTFX", 4) == 0) {
			/*
			This chunk is an array of integers that are 1 or 0. 1 means that the texture at the same position in the MTEX array has to be handled differentely. The size of this chunk is always the same as there are entries in the MTEX chunk.
			Simple as it is:
			struct MTFX 
			{
			uint32 mode[nMTEX];
			}
			The textures with this extended rendering mode are no normal ones, but skyboxes. These skyboxes are getting added as a reflection layer on the terrain. This is used for icecubes reflecting clouds etc. The layer being the reflection one needs to have the 0x400 flag in the MCLY chunk.
			*/
		}
		else {
			wxLogMessage(wxT("No implement tile chunk %s [%d]."), fourcc, size);
		}

		f.seek((int)nextpos);
	}

	// read individual map chunks
	for (ssize_t j=0; j<CHUNKS_IN_TILE; j++) {
		for (size_t i=0; i<CHUNKS_IN_TILE; i++) {
			if (mcnk_offsets[j*CHUNKS_IN_TILE+i] == 0 || mcnk_sizes[j*CHUNKS_IN_TILE+i] == 0) {
				continue;
			}
			f.seek((int)mcnk_offsets[j*16+i]);
			chunks[j][i].init(this, f, mBigAlpha);
		}
	}

	// init quadtree
	topnode.setup(this);

	f.close();
}

MapTile::~MapTile()
{
	if (!ok) return;

	wxLogMessage(wxT("Unloading tile %d,%d"), x, z);

	topnode.cleanup();

	for (ssize_t j=0; j<CHUNKS_IN_TILE; j++) {
		for (size_t i=0; i<CHUNKS_IN_TILE; i++) {
			chunks[j][i].destroy();
		}
	}

	for (size_t j=0; j<textures.size(); j++) {
		texturemanager.delbyname(textures[j]);
	}

	/*
	// TODO
	for (vector<string>::iterator it = wmos.begin(); it != wmos.end(); ++it) {
		gWorld->wmomanager.delbyname(*it);
	}

	for (vector<string>::iterator it = models.begin(); it != models.end(); ++it) {
		gWorld->modelmanager.delbyname(*it);
	}
	*/
}

void MapTile::draw()
{
	if (!ok) return;

	glRotatef(viewrot.y, 1, 0, 0);
	glRotatef(viewrot.x, 0, 1, 0);
	//glTranslatef(0,0,-100);
	//glTranslatef(viewpos.x, viewpos.y, viewpos.z);

	Vec3D camera;
	Vec3D lookat;

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);

	//glMatrixMode(GL_MODELVIEW);
	//glLoadIdentity();
	camera.x = viewpos.x;
	camera.y = viewpos.y;
	camera.z = viewpos.z;
	lookat.x = camera.x + 1.0f;
	lookat.y = camera.y - 0.5f;
	lookat.z = camera.z + 1.0f;
	gluLookAt(camera.x,camera.y,camera.z, lookat.x,lookat.y,lookat.z, 0.0f, 1.0f, 0.0f);

	// Draw height map
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL); // less z-fighting artifacts this way, I think
	glEnable(GL_LIGHTING);

 	glEnable(GL_COLOR_MATERIAL);
	//glColorMaterial(GL_FRONT, GL_DIFFUSE);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
	glColor4f(1,1,1,1);
	// if we're using shaders let's give it some specular
	if (video.supportShaders) {
		Vec4D spec_color(1,1,1,1);
		glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec_color);
		glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 20);

		glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
	}

	glEnable(GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, gdetailtexcoords);
	glTexCoordPointer(2, GL_FLOAT, 0, 0);

	glClientActiveTextureARB(GL_TEXTURE1_ARB);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, galphatexcoords);
	glTexCoordPointer(2, GL_FLOAT, 0, 0);

	glClientActiveTextureARB(GL_TEXTURE0_ARB);

	for (ssize_t j=0; j<CHUNKS_IN_TILE; j++) {
		for (size_t i=0; i<CHUNKS_IN_TILE; i++) {
			chunks[j][i].visible = false;
			chunks[j][i].draw();
		}
	}

	topnode.draw();

}

void MapTile::drawWater()
{
	if (!ok) return;

	for (ssize_t j=0; j<CHUNKS_IN_TILE; j++) {
		for (size_t i=0; i<CHUNKS_IN_TILE; i++) {
			if (chunks[j][i].visible) 
				chunks[j][i].drawWater();
		}
	}
}

void MapTile::drawObjects()
{
	if (!ok) return;

	for (size_t i=0; i<nWMO; i++) {
		//wmois[i].draw();
	}
}

void MapTile::drawSky()
{
	if (!ok) return;

	for (size_t i=0; i<nWMO; i++) {
		//wmois[i].wmo->drawSkybox();
		//if (gWorld->hadSky) break; // TODO
	}
}

/*
void MapTile::drawPortals()
{
if (!ok) return;

for (size_t i=0; i<nWMO; i++) {
wmois[i].drawPortals();
}
}
*/

void MapTile::drawModels()
{
	if (!ok) return;

	for (size_t i=0; i<nMDX; i++) {
		//modelis[i].draw();
	}
}

int holetab_h[4] = {0x1111, 0x2222, 0x4444, 0x8888};
int holetab_v[4] = {0x000F, 0x00F0, 0x0F00, 0xF000};

bool isHole(int holes, int i, int j)
{
	return (holes & holetab_h[i] & holetab_v[j])!=0;
}


int indexMapBuf(int x, int y)
{
	return ((y+1)/2)*9 + (y/2)*8 + x;
}

/*
MCNK chunk
http://www.madx.dk/wowdev/wiki/index.php?title=ADT/v18
After the above mentioned chunks come 256 individual MCNK chunks, row by row, starting from top-left (northwest). The MCNK chunks have a large block of data that starts with a header, and then has sub-chunks of its own.
Each map chunk has 9x9 vertices, and in between them 8x8 additional vertices, several texture layers, normal vectors, a shadow map, etc.
The MCNK header is 128 bytes large.
*/

enum // 03-29-2005 By ObscuR
{
	FLAG_MCSH,
	FLAG_IMPASS,
	FLAG_LQ_RIVER,
	FLAG_LQ_OCEAN,
	FLAG_LQ_MAGMA,
	FLAG_MCCV,
};

#define	MCLY_USE_ALPHAMAP		0x100
#define	MCLY_ALPHAMAP_COMPRESS	0x200
struct MCLY
{
	uint32 textureId;
	uint32 flags;
	uint32 offsetInMCAL;
	int16 effectId;
	int16 padding;
};

/*
The X' and Z' coordinates specify the top left corner of the map chunk, but they are in an alternate coordinate system! (yay for consistency!) This one has its 0,0 point in the middle of the world, and has the axes' directions reversed.
X = 32*533.3333 - X'
Z = 32*533.3333 - Z'
The Y base coordinate acts as a 'zero point' for the height values in the upcoming height map - that is, all height values are added to this Y height.
About the holes in the terrain: This is a bitmapped field, the least significant 16 bits are used row-wise in the following arrangement with a 1 bit meaning that the map chunk has a hole in that part of its area:
0x1	0x2	0x4	0x8
0x10	0x20	0x40	0x80
0x100	0x200	0x400	0x800
0x1000	0x2000	0x4000	0x8000
With this I've been able to fix some holes, the most obvious one being the Gates of Ironforge but there are some glitches for some other holes like the Lakeshire Inn - maybe this would require the more detailed height maps or for me to use a less cheap way to omit the hole triangles. :)
flags:
Flag		Meaning
0x1 		MCSH chunk available
0x2 		Impassable?
0x4 		River
0x8 		Ocean 
0x10		Magma
0x20		Slime?
0x40		MCCV chunk available
0x8000		Unknown, but heavily used in TBC.
*/
void MapChunk::initTextures(wxString basename, int first, int last)
{
	wxString buf;
	for (ssize_t i=first; i<=last; i++) {
		buf = wxString::Format(wxT("%s.%d.blp"), (char *)basename.c_str(), i);
		wTextures.push_back(texturemanager.add(buf));
	}
}

static unsigned char blendbuf[64*64*4]; // make unstable when new/delete, just make it global
static unsigned char amap[64*64];
void MapChunk::init(MapTile* mt, MPQFile &f, bool bigAlpha)
{
	//Vec3D tn[mapbufsize], tv[mapbufsize];
	
	maptile = mt;

	char fcc[5];
	uint32 size;

	size_t mcnk_pos = f.getPos();

	f.read(fcc,4); // MCNK
	f.read(&size, 4);
	flipcc(fcc);
	fcc[4] = 0;

	if (strncmp(fcc, "MCNK", 4)!=0 || size == 0) {
		wxLogMessage(wxT("Error: mcnk main chunk %s [%d]."), fcc, size);
		return;
	}

	// okay here we go ^_^
	mBigAlpha=bigAlpha;
	
	size_t lastpos = f.getPos() + size;

	//char header[0x80];
	//MapChunkHeader header;
	f.read(&header, 0x80);

	areaID = header.areaid;

	zbase = header.zpos;
	xbase = header.xpos;
	ybase = header.ypos;

	int holes = header.holes;
	int chunkflags = header.flags;

	/*
	0x4 		River
	0x8 		Ocean 
	0x10		Magma
	0x20		Slime?
	*/

	//if (chunkflags & 4)
	{
		// river / lakes
		initTextures(wxT("XTextures\\river\\lake_a"), 1, 30); // TODO: rivers etc.?
	}
	//else if (chunkflags & 8)
	/*{
		// ocean
		initTextures("XTextures\\ocean\\ocean_h", 1, 30);
	}
	else if (chunkflags & 16)
	{
		// magma:
		initTextures("XTextures\\lava\\lava", 1, 30);
	}*/

	/*
	bool comp[3] = { false, false, false };
	bool comp2[3] = { false, false, false };
	unsigned ofs[3] = { 0, 0, 0 };
	*/
	struct MCLY mcly[4];
	memset(mcly, 0, sizeof(struct MCLY)*4);
	//size_t comp_sizes[4] = { 0, 0, 0, 0 };
	hasholes = (holes != 0);

	/*
	if (hasholes) {
	gLog("Holes: %d\n", holes);
	int k=1;
	for (ssize_t j=0; j<4; j++) {
	for (size_t i=0; i<4; i++) {
	gLog((holes & k)?"1":"0");
	k <<= 1;
	}
	gLog("\n");
	}
	}
	*/

	// correct the x and z values ^_^
	zbase = zbase*-1.0f + ZEROPOINT;
	xbase = xbase*-1.0f + ZEROPOINT;

	vmin = Vec3D( 9999999.0f, 9999999.0f, 9999999.0f);
	vmax = Vec3D(-9999999.0f,-9999999.0f,-9999999.0f);

	//unsigned char *blendbuf;
	if (video.supportShaders) {
		//blendbuf = new unsigned char[64*64*4];
		memset(blendbuf, 0, 64*64*4);
	}

	while (f.getPos() < lastpos) {
		memset(fcc, 0, 4);
		size = 0;
		f.read(fcc,4);
		f.read(&size, 4);
		flipcc(fcc);
		fcc[4] = 0;

		//gLog("fcc: %s, size: %d, pos: %d, size: %d.\n", fcc, size, f.getPos(), f.getSize());

		if (size == 0) {
			// MCAL always has wrong size....
			if (strncmp(fcc, "MCAL", 4) == 0 && (size+8) != header.sizeAlpha) {
				f.read(fcc,4);
				flipcc(fcc);
				fcc[4] = 0;
				size_t nextpos;
				if (strncmp(fcc, "MCLQ", 4) == 0) {
					nextpos = f.getPos() + size;
				} else {
					nextpos = mcnk_pos+header.ofsAlpha+header.sizeAlpha;
				}
				f.seek((int)nextpos);
				//gLog("mcnk %d, pos %d, size %d, ofsAlpha %d, sizeAlpha %d, nextpos %d\n", mcnk_pos, f.getPos(), size, header.ofsAlpha, header.sizeAlpha, nextpos);
			}
			// MCLQ sometimes has wrong size, like StratholmeCOT
			if (strncmp(fcc, "MCLQ", 4) == 0) // nothing behind MCLQ, just break;
				break;
			if (strncmp(fcc, "MCLQ", 4) == 0 && (size+8) != header.sizeLiquid) {
				f.read(fcc,4);
				flipcc(fcc);
				fcc[4] = 0;
				size_t nextpos;
				if (strncmp(fcc, "MCSE", 4) == 0) {
					nextpos = f.getPos() + size;
				} else {
					nextpos = mcnk_pos+header.ofsSndEmitters;
					f.seek((int)nextpos);
					f.read(fcc, 4);
					flipcc(fcc);
					fcc[4] = 0;
					if (strncmp(fcc, "MCSE", 4) != 0)
						break; // nothing behind MCLQ, just break;
				}
				f.seek((int)nextpos);
				//gLog("mcnk %d, pos %d, size %d, ofsAlpha %d, sizeAlpha %d, nextpos %d\n", mcnk_pos, f.getPos(), size, header.ofsAlpha, header.sizeAlpha, nextpos);
			}
			continue;
		}

		size_t nextpos = f.getPos() + size;

		if (fcc[0] != 'M' || f.getPos() > f.getSize()) {
			wxLogMessage(wxT("Error: mcnk chunk initial error, fcc: %s, size: %d, pos: %d, size: %d."), fcc, size, f.getPos(), f.getSize());
			break;
		}

		if (strncmp(fcc, "MCVT", 4) == 0) {
			/*
			These are the actual height values for the 9x9+8x8 vertices. 145 floats in the following order/arrangement:.
			1    2	3	 4	  5    6	7	 8	  9
			10	11	 12   13   14	15	 16   17
			18   19   20	21	 22   23   24	25	 26
			27	28	 29   30   31	32	 33   34
			35   36   37	38	 39   40   41	42	 43
			44	45	 46   47   48	49	 50   51
			52   53   54	55	 56   57   58	59	 60
			61	62	 63   64   65	66	 67   68
			69   70   71	72	 73   74   75	76	 77
			78	79	 80   81   82	83	 84   85
			86   87   88	89	 90   91   92	93	 94
			95	96	 97   98  99  100  101	102
			103  104  105  106	107  108  109  110	111
			112  113	114  115  116  117	118  119
			120  121  122  123	124  125  126  127	128
			129  130	131  132  133  134	135  136
			137  138  139  140	141  142  143  144	145
			The inner 8 vertices are only rendered in WoW when its using the up-close LoD. Otherwise, it only renders the outer 9. Nonsense? If I only change one of these it looks like: [1].
			Ok, after a further look into it, WoW uses Squares out of 4 of the Outer(called NoLoD)-Vertices with one of the Inner(called LoD)-Vertices in the Center:
			*/
			Vec3D *ttv = tv;

			// vertices
			for (ssize_t j=0; j<17; j++) {
				for (size_t i=0; i<((j%2)?8:9); i++) {
					float h,xpos,zpos;
					f.read(&h,4);
					xpos = i * UNITSIZE;
					zpos = j * 0.5f * UNITSIZE;
					if (j%2) {
						xpos += UNITSIZE*0.5f;
					}
					Vec3D v = Vec3D(xbase+xpos, ybase+h, zbase+zpos);
					*ttv++ = v;
					if (v.y < vmin.y) vmin.y = v.y;
					if (v.y > vmax.y) vmax.y = v.y;
				}
			}

			vmin.x = xbase;
			vmin.z = zbase;
			vmax.x = xbase + 8 * UNITSIZE;
			vmax.z = zbase + 8 * UNITSIZE;
			r = (vmax - vmin).length() * 0.5f;

		}
		else if (strncmp(fcc, "MCNR", 4) == 0) {
			/*
			MCNR sub-chunk
			Normal vectors for each vertex, encoded as 3 signed bytes per normal, in the same order as specified above.
			The size field of this chunk is wrong! Actually, it isn't wrong, but there are a few junk (unknown?) bytes between the MCNR chunk's end and the next chunk. The size of the MCNR chunk should be adjusted to 0x1C0 bytes instead of 0x1B3 bytes.
			Normals are stored in X,Z,Y order, with 127 being 1.0 and -127 being -1.0. The vectors are normalized.
			--Log 09:23, 28 May 2006 (EEST) Maybe the extra data are "edge flag" bitmaps that are used only by the client for smoothing normals around adjustment triangles ? I didn't check it, just a hint.
			*/
			nextpos = f.getPos() + 0x1C0; // size fix
			// normal vectors
			char nor[3];
			Vec3D *ttn = tn;
			for (ssize_t j=0; j<17; j++) {
				for (size_t i=0; i<((j%2)?8:9); i++) {
					f.read(nor,3);
					// order Z,X,Y ?
					//*ttn++ = Vec3D((float)nor[0]/127.0f, (float)nor[2]/127.0f, (float)nor[1]/127.0f);
					*ttn++ = Vec3D(-(float)nor[1]/127.0f, (float)nor[2]/127.0f, -(float)nor[0]/127.0f);
				}
			}
		}
		else if (strncmp(fcc, "MCLY", 4) == 0) {
			/*
			MCLY sub-chunk
			Complete and right as of 19-AUG-09 (3.0.9 or higher)
			Texture layer definitions for this map chunk. 16 bytes per layer, up to 4 layers.
			Every texture layer other than the first will have an alpha map to specify blending amounts. The first layer is rendered with full opacity. To know which alphamap is used, there is an offset into the MCAL chunk. That one is relative to MCAL.
			You can animate these by setting the flags. Only simple linear animations are possible. You can specify the direction in 45¢X steps and the speed.
			The textureId is just the array index of the filename array in the MTEX chunk.
			For getting the right feeling when walking, you should set the effectId which links to GroundEffectTexture.dbc. It defines the little detaildoodads as well as the footstep sounds and if footprints are visible. You only need to have the upper layer with an id and you can set the id to -1 (int16!) to have no detaildoodads and footsteps at all (?).
			Introduced in Wrath of the Lich King, terrain can now reflect a skybox. This is used for icecubes made out of ADTs to reflect something. You need to have the MTFX chunk in, if you want that. Look at an skybox Blizzard made to see how you should do it.
			struct SMLayer // 03-29-2005 By ObscuR, --schlumpf_ 19:47, 19 August 2009 (CEST)
			{
			000h  UINT32 textureId; 
			004h  UINT32 flags;		
			008h  UINT32 offsetInMCAL;
			00Ch  INT32 effectId;			// (actually int16 and padding)
			010h  		
			};
			Flag	 Description
			0x001	 Animation: Rotate 45¢X clockwise.
			0x002	 Animation: Rotate 90¢X clockwise.
			0x004	 Animation: Rotate 180¢X clockwise.
			0x008	 Animation: Make this faster.
			0x010	 Animation: Faster!!
			0x020	 Animation: Faster!!!!
			0x040	 Animation: Animate this texture as told in the other bits.
			0x080	 This will make the texture way brighter. Used for lava to make it "glow".
			0x100	 Use alpha map - set for every layer after the first
			0x200	 Alpha map is compressed (see MCAL chunk description)
			0x400	 Shiny! This layer adds reflection of the skybox in the texture. You should add a MTFX chunk.
			*/
			// texture info
			nTextures = (int)size / 16;
			//gLog("=\n");
			for (size_t i=0; i<nTextures; i++) {
				f.read(&mcly[i], sizeof(struct MCLY));

				if (mcly[i].flags & 0x80) {
					animated[i] = mcly[i].flags;
				} else {
					animated[i] = 0;
				}

				textures[i] = texturemanager.get(mt->textures[mcly[i].textureId]);
			}
		}
		else if (strncmp(fcc, "MCRF", 4) == 0) {
			/*
			A list of with MCNK.nDoodadRefs + MCNK.nMapObjRefs indices into the file's MDDF and MODF chunks, saying which MCNK subchunk those particular doodads and objects are drawn within. This MCRF list contains duplicates for map doodads that overlap areas.
			As both, WMOs and M2s are referenced here, they get doodad indices first, then WMOs. If you have a doodad and a WMO in the ADT as well as the MCNK, you will have a {0,0} in MCRF with nDoodadRefs and MCNK.nMapObjRefs being 1.
			*/
		}
		else if (strncmp(fcc, "MCAL", 4) == 0) {
			/*
			Alpha maps for additional texture layers. For every layer, a 32x64 array of alpha values. Can be used as a secondary texture with the modulation op to control the blending of the texture layers. For video cards with 2 texture units, this requires one pass per layer. (For 4 or more texture units, maybe this could be done faster using register combiners? Pixel shaders maybe?)
			The size field of this chunk might be wrong for map chunks with zero texture layers. There are a couple of these in some of the development maps.
			Similar to the shadow maps, the 32x64 bytes is really a 64x64 alpha map. There are 2 alpha values per byte, first 4 bits and second 4 bits. -Eric
			Funny how this happened to work for the wrong sizes, too, because the upper 4 bits became the most significant in the alpha map, and the lower 4 appeared as noise. I sure didn't notice the difference :) It's fixed now of course. - Z.
			Flow - 21-10-2008:
			In Wotlk these chunks are compressed.
			I think it's a self made compression. Really simple. The size field of this chunk doesn't matter anymore.
			How the decompression works
			read a byte
			check for sign bit
			if set we are in fill mode else we are in copy mode
			take the 7 lesser bits of the first byte as a count indicator
			fill mode: read the next byte an fill it by count in resulting alpha map
			copy mode: read the next count bytes and copy them in the resulting alpha map
			if the alpha map is complete we are done otherwise start at 1. again
			Sample C++ code
			// 21-10-2008 by Flow
			unsigned offI = 0; //offset IN buffer
			unsigned offO = 0; //offset OUT buffer
			char* buffIn; // pointer to data in adt file
			char buffOut[4096]; // the resulting alpha map

			while( offO < 4096 )
			{
			// fill or copy mode
			bool fill = buffIn[offI] & 0x80;
			unsigned n = buffIn[offI] & 0x7F;
			offI++;
			for( unsigned k = 0; k < n; k++ )
			{
			buffOut[offO] = buffIn[offI];
			offO++;
			if( !fill )
			offI++;
			}
			if( fill ) offI++;
			}
			Tested with Wotlk Beta build 8770
			Note: not all alpha maps are compressed; some of them are just stored as 4096 plain bytes. If an alpha map is compressed can be determined by checking the compression flag in the corresponding MCLY chunk entry. - Slartibartfast
			In order to know whether you deal with an uncompressed alphamap of 2048 bytes (preWotLK) or 4096 bytes (postWotLK), you can look at the size value for this SubChunk. If it is not zero and a multiple of 2048, it seems to be preWotLK. - Hamunaptra
			Slartibartfast - 1 November 2008:
			Blizzard has changed the way how the additional textures are blended onto the ground texture in Northrend (old continents still seem to be blended the old way; they also don't use the new alpha map format). They have gone from a "one-layer-per-step" approach to blending all the 4 textures in a single step according to the following formula:
			finalColor = tex0 * (1.0 - (alpha1 + alpha2 + alpha3)) + tex1 * alpha1 + tex2 * alpha2 + tex3 * alpha3
			So all the alpha values for the different layers including the ground layer add up to 1.0; the ground layer's alpha value is calculated to match this constraint.

			How to render

			It is of course possible to devise different ways to render such terrain; one way I use and of which I know that it's working is a 2-pass-approach: first render all ground textures without blending, then use a fragment shader program to mix the 1-3 additional layer textures and render them with a glBlendFunc setting of (GL_ONE, GL_ONE_MINUS_SRC_ALPHA) on top of the ground texture already present in the framebuffer. The fragment program that mixes the textures would have to work like this short GLSL example:
			gl_FragColor =   texture2D(texture0, vec2(gl_TexCoord[0])) * texture2D(texture3, vec2(gl_TexCoord[3])).r
			+ texture2D(texture1, vec2(gl_TexCoord[1])) * texture2D(texture3, vec2(gl_TexCoord[3])).g
			+ texture2D(texture2, vec2(gl_TexCoord[2])) * texture2D(texture3, vec2(gl_TexCoord[3])).b;
			(this example uses 4 texture units: texture0 - texture3; the first 3 of them contain the actual textures, while the fourth unit contains the alpha maps combined in one RGB texture)

			*/
			// alpha maps  64 x 64 = 4096
			unsigned char *data = f.getPointer();
			if (nTextures>0 && data) {
				glGenTextures(nTextures-1, alphamaps);
				/*
				gLog("MCAL %d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d - %d\n", 
				mcly[0].flags&MCLY_USE_ALPHAMAP, 
				mcly[1].flags&MCLY_USE_ALPHAMAP,
				mcly[2].flags&MCLY_USE_ALPHAMAP,
				mcly[3].flags&MCLY_USE_ALPHAMAP,
				mcly[0].flags&MCLY_ALPHAMAP_COMPRESS,
				mcly[1].flags&MCLY_ALPHAMAP_COMPRESS,
				mcly[2].flags&MCLY_ALPHAMAP_COMPRESS,
				mcly[3].flags&MCLY_ALPHAMAP_COMPRESS,
				mcly[0].offsetInMCAL,
				mcly[1].offsetInMCAL,
				mcly[2].offsetInMCAL,
				mcly[3].offsetInMCAL,
				comp_sizes[0], comp_sizes[1], comp_sizes[2], comp_sizes[3], header.sizeAlpha);
				*/
				for (ssize_t i=1; i<nTextures; i++) {
					// Alfred, error check
					if ((mcly[i].flags & MCLY_USE_ALPHAMAP) == 0)
						continue;
					glBindTexture(GL_TEXTURE_2D, alphamaps[i-1]);

					unsigned char *abuf = data + mcly[i].offsetInMCAL;
					if (mcly[i].flags&MCLY_ALPHAMAP_COMPRESS) { // compressed
						// 21-10-2008 by Flow
						unsigned int offI = 0; //offset IN buffer
						unsigned int offO = 0; //offset OUT buffer
						while( offO < 64*64 )
						{
							// fill or copy mode
							bool fill = (abuf[offI] & 0x80) > 0;
							unsigned int n = abuf[offI] & 0x7F;
							offI++;
							for( unsigned int k = 0; k < n; k++ )
							{
								if (offO >= 64*64)
									break;
								amap[offO] = abuf[offI];
								offO++;
								if (!fill)
									offI++;
							}
							if (fill)
								offI++;
						}
						//f.seekRelative(offI);
					} else if (mBigAlpha) {
						if (f.getPos() + mcly[i].offsetInMCAL + 0x1000 > f.getSize())
							continue;
						unsigned char *p;
						p = amap;
						for (ssize_t j=0; j<64; j++) {
							for (size_t i=0; i<64; i++) {
								*p++ = *abuf++;
							}

						}
						memcpy(amap+63*64,amap+62*64,64);
						//f.seekRelative(0x1000);
					} else {
						unsigned char *p;
						p = amap;
						for (ssize_t j=0; j<64; j++) {
							for(int k=0; k<32; k++) {
								unsigned char c = *abuf++;
								if(i!=31)
								{
									*p++ = (unsigned char)((255*((int)(c & 0x0f)))/0x0f);
									*p++ = (unsigned char)((255*((int)(c & 0xf0)))/0xf0);
								}
								else
								{
									*p++ = (unsigned char)((255*((int)(c & 0x0f)))/0x0f);
									*p++ = (unsigned char)((255*((int)(c & 0x0f)))/0x0f);
								}
							}
						}
						memcpy(amap+63*64,amap+62*64,64);
						//f.seekRelative(64*32);
					}
					glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 64, 64, 0, GL_ALPHA, GL_UNSIGNED_BYTE, amap);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

					if (video.supportShaders) {
						for (ssize_t p=0; p<64*64; p++) {
							blendbuf[p*4+i-1] = amap[p];
						}
					}
				}

			}
			// some MCAL chunks have incorrect sizes! :(
			if ((size+8) != header.sizeAlpha) {
				// continue;
				nextpos = mcnk_pos+header.ofsAlpha+header.sizeAlpha;
				//gLog("mcnk %d, pos %d, size %d, ofsAlpha %d, sizeAlpha %d, nextpos %d\n", mcnk_pos, f.getPos(), size, header.ofsAlpha, header.sizeAlpha, nextpos);
			}
		}
		else if (strncmp(fcc,"MCSH", 4) == 0) {
			// shadow map 64 x 64
			unsigned char sbuf[64*64], *p, c[8];
			p = sbuf;
			for (ssize_t j=0; j<64; j++) {
				f.read(c,8);
				for (size_t i=0; i<8; i++) {
					for (ssize_t b=0x01; b!=0x100; b<<=1) {
						*p++ = (c[i] & b) ? 85 : 0;
					}
				}
			}
			glGenTextures(1, &shadow);
			glBindTexture(GL_TEXTURE_2D, shadow);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 64, 64, 0, GL_ALPHA, GL_UNSIGNED_BYTE, sbuf);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			if (video.supportShaders) {
				for (ssize_t p=0; p<64*64; p++) {
					blendbuf[p*4+3] = sbuf[p];
				}
			}
		}
		else if (strncmp(fcc,"MCLQ", 4) == 0) {
			/*
			Water levels for this map chunk. This chunk is old and not really used anymore. Still, there is backwards compatibility in the client as old ADTs are not updated as it would be much data to patch it. I guess, it will be done in some expansion. You can fully use this chunk, even to have multiple water. You can have a lot of stacked water with this and the MH2O one. I advise you to implement the MH2O one as its better if you want to write a editor for ADT files.
			The size of the chunk is in the mapchunk header. The type of liquid is given in the mapchunk flags, also in the header.
			This information is old and incomplete as well as maybe wrong.
			The first two floats specify the minimum and maximum liquid height level. After them comes a 9x9 height map for the water with the following format per vertex:
			Offset 	Type 	Description
			0x00 	int16 	?
			0x02 	int16 	?
			0x04 	float 	height value
			The unknown int values might be color or transparency info, or something entirely different... Most frequently they are 0.
			Followed by 8x8 bytes of flags for every liquid "tile" between the 9x9 vertex grid. The value 0x0F means do not render. (the specific flag for this seems to be 8 but I'm not sure - but it fixes some places where there was extra "water" sticking into the rest of the scenery)
			Finally, 0x54 bytes of additional data, no idea what it's used for.

			*/
			// liquid / water level
			haswater = true;
			f.read(&waterlevel,8); // 2 values - Lowest water Level, Highest Water Level

			if (waterlevel[1] > vmax.y) vmax.y = waterlevel[1];
			//if (waterlevel < vmin.y) haswater = false;

			lq = new Liquid(8, 8, Vec3D(xbase, waterlevel[1], zbase));
			//lq->init(f);
			lq->initFromTerrain(f, chunkflags);
			
			/*
			// let's output some debug info! ( '-')b
			string lq = "";
			if (flags & 4) lq.append(" river");
			if (flags & 8) lq.append(" ocean");
			if (flags & 16) lq.append(" magma");
			if (flags & 32) lq.append(" slime?");
			gLog("LQ%s (base:%f)\n", lq.c_str(), waterlevel);
			*/
			break; // nothing behind MCLQ, just break;
		}
		else if (strncmp(fcc,"MCSE", 4) == 0) {
			/*
			Sound emitters.
			This seems to be a bit different to that structure, ObscuR posted back then. From what I can see, WoW takes only 0x1C bytes per entry. Quite a big difference. This change might have happened, when they introduced the SoundEntriesAdvanced.dbc.
			struct CWSoundEmitter // --schlumpf_ 21:44, 8 August 2009 (CEST)
			{
			000h  uint32 SoundEntriesAdvancedId;
			004h  Vec3F position;
			008h  
			00Ch  
			010h  Vec3F size; 					// I'm not really sure with this. I'm far too lazy to analyze this. Seems like noone ever needed these anyway.
			014h  
			018h  
			};
			*/
		}
		else if (!strncmp(fcc, "MCCV", 4)) {
			/*
			New Subchunk found in Northrend-adts(and only there as far as I see). Size seems to be always 0x244. Offset seems to be at 0x74 in the MCNK-header.
			--Tigurius:Found in WotLK-Beta 3.0.1

			The chunk contains 145 color values, each one is designated to one of the heightmap vertices. Each single color is defined in 4 bytes:
			Offset	Type		Description
			0x0 	byte		red value
			0x1 	byte		green value
			0x2 	byte		blue value
			0x3 	byte		alpha value? (usually 0xFF, changing this doesn't have any visible effect)
			The color components are one byte each, with 0x7F being 1.0, 0x00 being 0.0 and 0xFF being 2.0 (I am not sure about the 2.0 factor, but the visible change when setting a component to 0xFF suggests this value).
			When rendering, these color settings manipulate the final color of a pixel in a chunk by multiplication. Usually all the component values are set to 0x7F (= multiplication with 1.0), which means there's no modification at all. But if set to (for example) 0x00, a chunk can be made completely black, or if set to 0xFF, a chunk is rendered brighter than normal.
			--Slartibartfast 1 November 2008
			*/
			//gLog("No implement mcnk subchunk %s [%d].\n", fcc, size);
		}
		else {
			wxLogMessage(wxT("No implement mcnk subchunk %s [%d]."), fcc, size);
		}
		f.seek((int)nextpos);
	}

	// create vertex buffers
	glGenBuffersARB(1,&vertices);
	glGenBuffersARB(1,&normals);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vertices);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*3*sizeof(float), tv, GL_STATIC_DRAW_ARB);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, normals);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*3*sizeof(float), tn, GL_STATIC_DRAW_ARB);

	if (hasholes)
		initStrip(holes);
	/*
	else {
		strip = maptile->mapstrip2;
		striplen = stripsize2;
	}
	*/

	this->mt = mt;

	vcenter = (vmin + vmax) * 0.5f;

	if (video.supportShaders) {
		glGenTextures(1, &blend);
		glBindTexture(GL_TEXTURE_2D, blend);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, blendbuf);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		//delete[] blendbuf;
	}

#if 0
	deleted=false;
	nameID=addNameMapChunk(this);

	Vec3D *ttv = tm;

	// vertices
	for (ssize_t j=0; j<17; j++) {
		for (size_t i=0; i<((j%2)?8:9); i++) {
			float h,xpos,zpos;
			f.read(&h,4);
			xpos = i * 0.125f;
			zpos = j * 0.5f * 0.125f;
			if (j%2) {
				xpos += 0.125f*0.5f;
			}
			Vec3D v = Vec3D(xpos+px, zpos+py,-1);
			*ttv++ = v;
		}
	}

	if( (Flags&1) == 0 )
	{
		/** We have no shadow map (MCSH), so we got no shadows at all!	**
		** This results in everything being black.. Yay. Lets fake it! **/

		unsigned char sbuf[64*64];
		for( int j = 0; j < 4096; j++ ) 
			sbuf[j] = 0;

		glGenTextures( 1, &shadow );
		glBindTexture( GL_TEXTURE_2D, shadow );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_ALPHA, 64, 64, 0, GL_ALPHA, GL_UNSIGNED_BYTE, sbuf );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}

	float ShadowAmount;
	for (ssize_t j=0; j<mapbufsize;j++)
	{
		//tm[j].z=tv[j].y;
		ShadowAmount=1.0f-(-tn[j].x+tn[j].y-tn[j].z);
		if(ShadowAmount<0)
			ShadowAmount=0.0f;
		if(ShadowAmount>1.0)
			ShadowAmount=1.0f;
		ShadowAmount*=0.5f;
		//ShadowAmount=0.2;
		ts[j].x=0;
		ts[j].y=0;
		ts[j].z=0;
		ts[j].w=ShadowAmount;
	}

	glGenBuffersARB(1,&minimap);
	glGenBuffersARB(1,&minishadows);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, minimap);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*3*sizeof(float), tm, GL_STATIC_DRAW_ARB);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, minishadows);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*4*sizeof(float), ts, GL_STATIC_DRAW_ARB);
#endif
}


void MapChunk::initStrip(int holes)
{
	strip = new short[256]; // TODO: figure out exact length of strip needed
	short *s = strip;
	bool first = true;
	for (ssize_t y=0; y<4; y++) {
		for (ssize_t x=0; x<4; x++) {
			if (!isHole(holes, x, y)) {
				// draw tile here
				// this is ugly but sort of works
				int i = x*2;
				int j = y*4;
				for (ssize_t k=0; k<2; k++) {
					if (!first) {
						*s++ = indexMapBuf(i,j+k*2);
					} else first = false;
					for (ssize_t l=0; l<3; l++) {
						*s++ = indexMapBuf(i+l,j+k*2);
						*s++ = indexMapBuf(i+l,j+k*2+2);
					}
					*s++ = indexMapBuf(i+2,j+k*2+2);
				}
			}
		}
	}
	striplen = (int)(s - strip);
}


void MapChunk::destroy()
{
	// unload alpha maps
	glDeleteTextures(nTextures-1, alphamaps);
	// shadow maps, too
	glDeleteTextures(1, &shadow);

	// delete VBOs
	glDeleteBuffersARB(1, &vertices);
	glDeleteBuffersARB(1, &normals);

	if (hasholes) delete[] strip;

	if (haswater) delete lq;
}

void MapChunk::drawPass(int anim)
{
	if (anim) {
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();

		// note: this is ad hoc and probably completely wrong
		//int spd = (anim & 0x08) | ((anim & 0x10) >> 2) | ((anim & 0x20) >> 4) | ((anim & 0x40) >> 6);
		int dir = anim & 0x07;
		const float texanimxtab[8] = {0, 1, 1, 1, 0, -1, -1, -1};
		const float texanimytab[8] = {1, 1, 0, -1, -1, -1, 0, 1};
		float fdx = -texanimxtab[dir], fdy = texanimytab[dir];

		//int detail_size = 1;
		//int animspd = (int)(200.0f * detail_size);
		//float f = ( ((int)(gWorld->animtime*(spd/15.0f))) % animspd) / (float)animspd;
		int f = 0; // TODO
		glTranslatef(f*fdx,f*fdy,0);
	}

	glDrawElements(GL_TRIANGLE_STRIP, striplen, GL_UNSIGNED_SHORT, strip);

	if (anim) {
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glActiveTextureARB(GL_TEXTURE1_ARB);
	}
}


void MapChunk::draw()
{
	//if (!gWorld->frustum.intersects(vmin,vmax)) return; // TODO
	if (nTextures == 0) return;
	//float mydist = (gWorld->camera - vcenter).length() - r; // TODO
	//if (mydist > gWorld->mapdrawdistance2) return;
	//if (mydist > gWorld->culldistance) { // TODO
		//if (gWorld->uselowlod) this->drawNoDetail();
		//return;
	//}
	visible = true;

	if (nTextures==0) return;

	if (!hasholes) {
		strip = maptile->mapstrip2;
		striplen = stripsize2;
	}
	/*
	// TODO
	if (!hasholes) {
		bool highres = gWorld->drawhighres;
		if (highres) {
			highres = mydist < gWorld->highresdistance2;
		}
		if (highres) {
			strip = gWorld->mapstrip2;
			striplen = stripsize2;
		} else {
			strip = gWorld->mapstrip;
			striplen = stripsize;
		}
	}
	*/

	// setup vertex buffers
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vertices);
	glVertexPointer(3, GL_FLOAT, 0, 0);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, normals);
	glNormalPointer(GL_FLOAT, 0, 0);
	// ASSUME: texture coordinates set up already

	if (video.supportShaders) {
		// SHADER-BASED

		// TODO: figure out texture animation for shaders
		// (modifying the texture matrix for only an individual texture layer)

		// setup textures
		/*
		unit 0 - base texture layer
		unit 1 - shadow map and alpha layers
		unit 2 - texture layer 1
		unit 3 - texture layer 2
		unit 4 - texture layer 3
		*/
		// base layer
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_2D, textures[0]);
		// shadow map
		// TODO: handle case when there is no shadowmap?
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_2D, blend);
		// blended layers
		for (ssize_t i=1; i<nTextures; i++) {
			int tex = GL_TEXTURE2_ARB + i - 1;
			glActiveTextureARB(tex);
			glBindTexture(GL_TEXTURE_2D, textures[i]);
		}
		glActiveTextureARB( GL_TEXTURE0_ARB );

		terrainShaders[nTextures-1]->bind();
		// setup shadow color as local parameter:
		//Vec3D shc = gWorld->skies->colorSet[SHADOW_COLOR] * 0.3f;
		//glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0, shc.x,shc.y,shc.z,1);
		glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0, 0.09f, 0.07f, 0.05f, 0.9f);
		//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDrawElements(GL_TRIANGLE_STRIP, striplen, GL_UNSIGNED_SHORT, strip);

		terrainShaders[nTextures-1]->unbind();
	} else {
		// FIXED-FUNCTION

		// first pass: base texture
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, textures[0]);

		glActiveTextureARB(GL_TEXTURE1_ARB);
		glDisable(GL_TEXTURE_2D);

		glDisable(GL_BLEND);
		drawPass(animated[0]);
		glEnable(GL_BLEND);

		if (nTextures>1) {
			//glDepthFunc(GL_EQUAL); // GL_LEQUAL is fine too...?
			glDepthMask(GL_FALSE);
		}

		// additional passes: if required
		for (size_t i=0; i<nTextures-1; i++) {
			glActiveTextureARB(GL_TEXTURE0_ARB);
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, textures[i+1]);
			// this time, use blending:
			glActiveTextureARB(GL_TEXTURE1_ARB);
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, alphamaps[i]);

			// if we loaded a texture with specular maps, setup the texenv
			// to replace our alpha channel instead of modulating it
			if (video.supportShaders) glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			drawPass(animated[i+1]);
			// back to normal
			if (video.supportShaders) glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		}

		if (nTextures>1) {
			//glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_TRUE);
		}

		// shadow map
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_LIGHTING);

		//Vec3D shc = gWorld->skies->colorSet[SHADOW_COLOR] * 0.3f;
		//glColor4f(0,0,0,1);
		//glColor4f(shc.x,shc.y,shc.z,1);

		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_2D, shadow);
		glEnable(GL_TEXTURE_2D);

		drawPass(0);

		glEnable(GL_LIGHTING);
		glColor4f(1,1,1,1);
	}

	/*
	//////////////////////////////////
	// debugging tile flags:
	GLfloat tcols[8][4] = {	{1,1,1,1},
	{1,0,0,1}, {1, 0.5f, 0, 1}, {1, 1, 0, 1},
	{0,1,0,1}, {0,1,1,1}, {0,0,1,1}, {0.8f, 0, 1,1}
	};
	glPushMatrix();
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
	glTranslatef(xbase, ybase, zbase);
	for (size_t i=0; i<8; i++) {
	int v = 1 << (7-i);
	for (ssize_t j=0; j<4; j++) {
	if (animated[j] & v) {
	glBegin(GL_TRIANGLES);
	glColor4fv(tcols[i]);

	glVertex3f(i*2.0f, 2.0f, j*2.0f);
	glVertex3f(i*2.0f+1.0f, 2.0f, j*2.0f);
	glVertex3f(i*2.0f+0.5f, 4.0f, j*2.0f);

	glEnd();
	}
	}
	}
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glColor4f(1,1,1,1);
	glPopMatrix();
	*/
}

void MapChunk::drawNoDetail()
{
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);

	//glColor3fv(gWorld->skies->colorSet[FOG_COLOR]);
	//glColor3f(1,0,0);
	//glDisable(GL_FOG);

	// low detail version
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vertices);
	glVertexPointer(3, GL_FLOAT, 0, 0);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDrawElements(GL_TRIANGLE_STRIP, striplen, GL_UNSIGNED_SHORT, strip);
	glEnableClientState(GL_NORMAL_ARRAY);

	glColor4f(1,1,1,1);
	//glEnable(GL_FOG);

	glEnable(GL_LIGHTING);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glEnable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glEnable(GL_TEXTURE_2D);
}



void MapChunk::drawWater()
{
	if( wTextures.size() == 0 )
		return;

	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glDisable(GL_TEXTURE_2D);

	//size_t texidx = (size_t)(gWorld->animtime / 60.0f) % wTextures.size();
	size_t texidx = 0;
	glBindTexture(GL_TEXTURE_2D, wTextures[texidx]);
	//glBindTexture(GL_TEXTURE_2D, gWorld->water);

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	
	//GLfloat col[2][4] = { { 0.f, 0.f, 1.f, 1.0f, }, { 0.f, 0.f, 1.f, 1.0f, }, };

	const float wr = 1.0f;
	for( unsigned asd = 0; asd < 2; asd++ )
	{
		if( asd == 1 )
		{
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			glEnable(GL_TEXTURE_2D);
		}

	for( unsigned l = 0; l < waterLayer.size(); l++ )
	{
		SWaterLayer& layer = waterLayer[l];

		unsigned ox = layer.x;
		unsigned oy = layer.y;

		for(unsigned y = oy; y < layer.h + oy; y++)
		{
			for( unsigned x = ox; x < layer.w + ox; x++)
			{
				unsigned tx = x - ox;
				unsigned ty = y - oy;

				// p1--p4
				// |    |  // this is GL_QUADS 
				// p2--p3
				unsigned p1 = tx     +   ty       * ( layer.w + 1 );
				unsigned p2 = tx     + ( ty + 1 ) * ( layer.w + 1 );
				unsigned p3 = tx + 1 + ( ty + 1 ) * ( layer.w + 1 );
				unsigned p4 = tx + 1 +   ty       * ( layer.w + 1 );

				// alpha values helper
				float a1,a2,a3,a4;
				a1 = a2 = a3 = a4 = 1.0f;
				if( layer.alphas.size() != 0 )
				{
					a1 = (float)layer.alphas[ p1 ] / 255.f * 1.5f + 0.3f; // whats the magic formular here ???
					a2 = (float)layer.alphas[ p2 ] / 255.f * 1.5f + 0.3f;
					a3 = (float)layer.alphas[ p3 ] / 255.f * 1.5f + 0.3f;
					a4 = (float)layer.alphas[ p4 ] / 255.f * 1.5f + 0.3f;
				}

				// height values helper
				float h1, h2, h3, h4;
				h1 = h2 = h3 = h4 = 0.0f;
				if( layer.heights.size() != 0 )
				{
					h1 = layer.heights[ p1 ];
					h2 = layer.heights[ p2 ];
					h3 = layer.heights[ p3 ];
					h4 = layer.heights[ p4 ];
				}

				if( layer.renderTiles.size() != 0 )
					if( !layer.renderTiles[ tx + ty * layer.w ] )
						continue;

				//glColor4f(1.0f, 1.0f, 1.0f);

				glBegin(GL_QUADS);

				if (asd == 0) glColor4f( 0, 1.0f, 0.9f, a1 ); else glColor4f( 1, 1, 1, 1 );
				glTexCoord2f(  0,  0 );
				//glNormal3f( 0, 1, 0 );
				glVertex3f( xbase            + UNITSIZE * x, h1, zbase            + UNITSIZE * y);

				if (asd == 0) glColor4f( 0, 1.0f, 0.9f, a2 ); else glColor4f( 1, 1, 1, 1 );
				glTexCoord2f(  0, wr );
				glNormal3f( 0, 1, 0 );
				glVertex3f( xbase            + UNITSIZE * x, h2, zbase + UNITSIZE + UNITSIZE * y);

				if (asd == 0) glColor4f( 0, 1.0f, 0.9f, a3 ); else glColor4f( 1, 1, 1, 1 );
				glTexCoord2f( wr, wr );
				glNormal3f( 0, 1, 0 );
				glVertex3f( xbase + UNITSIZE + UNITSIZE * x, h3, zbase + UNITSIZE + UNITSIZE * y);

				if (asd == 0) glColor4f( 0, 1.0f, 0.9f, a4 ); else glColor4f( 1, 1, 1, 1 );
				glTexCoord2f( wr,  0 );
				glNormal3f( 0, 1, 0 );
				glVertex3f( xbase + UNITSIZE + UNITSIZE * x, h4, zbase            + UNITSIZE * y);

				glEnd();
			}
		}
	}
	}

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void MapNode::draw()
{
	//if (!gWorld->frustum.intersects(vmin,vmax)) 
	//	return;
	for (size_t i=0; i<4; i++) 
		children[i]->draw();
}

void MapNode::setup(MapTile *t)
{
	vmin = Vec3D( 9999999.0f, 9999999.0f, 9999999.0f);
	vmax = Vec3D(-9999999.0f,-9999999.0f,-9999999.0f);
	mt = t;
	if (size==2) {
		// children will be mapchunks
		children[0] = &(mt->chunks[py][px]);
		children[1] = &(mt->chunks[py][px+1]);
		children[2] = &(mt->chunks[py+1][px]);
		children[3] = &(mt->chunks[py+1][px+1]);
	} else {
		int half = size / 2;
		children[0] = new MapNode(px, py, half);
		children[1] = new MapNode(px+half, py, half);
		children[2] = new MapNode(px, py+half, half);
		children[3] = new MapNode(px+half, py+half, half);
		for (size_t i=0; i<4; i++) {
			children[i]->setup(mt);
		}
	}
	for (size_t i=0; i<4; i++) {
		if (children[i]->vmin.x < vmin.x) vmin.x = children[i]->vmin.x;
		if (children[i]->vmin.y < vmin.y) vmin.y = children[i]->vmin.y;
		if (children[i]->vmin.z < vmin.z) vmin.z = children[i]->vmin.z;
		if (children[i]->vmax.x > vmax.x) vmax.x = children[i]->vmax.x;
		if (children[i]->vmax.y > vmax.y) vmax.y = children[i]->vmax.y;
		if (children[i]->vmax.z > vmax.z) vmax.z = children[i]->vmax.z;
	}
}

void MapNode::cleanup()
{
	if (size>2) {
		for (size_t i=0; i<4; i++) {
			children[i]->cleanup();
			delete children[i];
		}
	}
}

MapChunk *MapTile::getChunk(unsigned int x, unsigned int z)
{
	assert(x < CHUNKS_IN_TILE && z < CHUNKS_IN_TILE);
	return &chunks[z][x];
}
