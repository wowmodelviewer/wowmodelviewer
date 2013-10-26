#ifndef MODELEXPORT_M3_H
#define MODELEXPORT_M3_H

#include "modelheaders.h" // Sphere
#include "util.h"

/*
Most contents are retrived from http://code.google.com/p/libm3/w/list

Thanks dot.qwerty, madyavic
*/

/*
+--------------------------------+
|           MD34 Head            |
+--------------------------------+
|           MODL Head            |
+--------------------------------+
|          MODL Content          |
+--------------------------------+
|         ReferenceEntry         |
+--------------------------------+
*/

// Size = 16 byte / 0x10 byte
/*
struct QUAT
{
    float x, y, z, w;
};
*/

// Size = 32 byte / 0x20 byte
struct SphereF
{
	/*0x00*/ Vec3D min;
	/*0x0C*/ Vec3D max;
	/*0x18*/ float radius;
	/*0x1C*/ uint32 flags;
};

// Size = 68 byte / 0x44 byte
struct MatrixF
{
	/*0x00*/ Vec4D a;
	/*0x10*/ Vec4D b;
	/*0x20*/ Vec4D c;
	/*0x30*/ Vec4D d;
	/*0x40*/ uint32 flags;
};

/*
The file header contains the number of reference lists in the file, along with 
the offset where their headers start.

Each reference header contains the data type in the list, the number of entries 
in that list, along with the offset where that list starts.

There is a fourth value in the reference header, which purpose is unknown. But 
the size of the block can depend on this value. For example the MODL block in 
units/buildings have a different value here compared to the MODL block in 
skyboxes, and the size of the block / number of fields in the block is different.
*/
// Size = 12 byte / 0x0C byte
// Complete, struct HeadRef
struct Reference 
{
    /*0x00*/ uint32 nEntries; // nData
    /*0x04*/ uint32 ref; // indData
	/*0x08*/ uint32 flags;
};

// Size = 16 byte / 0x10 byte
// Incomplete, struct Tag
struct ReferenceEntry 
{ 
    /*0x00*/ char id[4]; // blockID
    /*0x04*/ uint32 offset; // ofs
    /*0x08*/ uint32 nEntries; // counts
    /*0x0C*/ uint32 vers; // Possibly nReferences;
};

/*
Animation References

Each animation sequence has related SD-blocks which references the data used by 
the animation. Animation references use a unique uint32 ID to reference sequence 
animation data found in STC. Following animation references are two values, the 
initial value before animation and another value of the same type that seems to 
have no effect. Following these is a uint32 flag that seems to always be 0. The 
value types depend on the animation reference (i.e. VEC3D, Quat, uint32, etc).
I'm willing to bet there's 13 different Animation Reference types based on the 
fact that [STC] has 13 static reference arrays for sequence data.
*/
// Size = 8 Byte / 0x08 byte
// Incomplete
struct AnimationReference
{
    /*0x00*/ uint16 flags; //usually 1
    /*0x02*/ uint16 animflag; //6 indicates animation data present
    /*0x04*/ uint32 animid; //a unique uint32 value referenced in STC.animid and STS.animid
};

// Size = 16 Byte / 0x10 byte
struct Aref_UINT16
{
    /*0x00*/ AnimationReference AnimRef; //STC/STS reference
    /*0x08*/ uint16 value; //initial value
    /*0x0A*/ uint16 unValue; //unused value
    /*0x0C*/ uint32 flag; //seems unused, 0
}; //used in SDS6 anim blocks, as I16_ data type

typedef struct
{
    AnimationReference AnimRef;
    short value[3]; //initial value
    short unValue[3]; //unused extra value
    uint32 flags; //unused, 0
} Aref_shrtVEC3D;

// Size = 20 Byte / 0x14 byte
struct Aref_UINT32
{
    /*0x00*/ AnimationReference AnimRef; //STC/STS reference
    /*0x08*/ uint32 value; //initial value
    /*0x0C*/ uint32 unValue; //unused value
    /*0x10*/ uint32 flag; //seems unused, 0
}; //used in SDFG anim blocks, as FLAG data type

// Size = 28 Byte / 0x1C byte
struct Aref_VEC2D
{
    /*0x00*/ AnimationReference AnimRef; //STC/STS reference
    /*0x08*/ Vec2D value; //initial value
    /*0x10*/ Vec2D unValue; //unused value
    /*0x18*/ uint32 flag; //seems unused, 0
}; //used in SD2V anim blocks, as VEC2 data type

// Size = 36 Byte / 0x24 byte
struct Aref_VEC3D
{
    /*0x00*/ AnimationReference AnimRef; //STC/STS reference
    /*0x08*/ Vec3D value; //initial value
    /*0x14*/ Vec3D unValue; //unused value
    /*0x20*/ uint32 flag; //seems unused, 0
}; //used in SD3V anim blocks, as VEC3 data type

// Size = 44 Byte / 0x2C byte
struct Aref_VEC4D
{
    /*0x00*/ AnimationReference AnimRef; //STC/STS reference
    /*0x08*/ Vec4D value; //initial value
    /*0x18*/ Vec4D unValue; //unused value
    /*0x28*/ uint32 flags; //seems unused, 0
}; //used in SD4Q anim blocks, as QUAT data type

// Size = 20 Byte / 0x14 byte
struct Aref_FLOAT
{
    /*0x00*/ AnimationReference AnimRef; //STC/STS reference
    /*0x08*/ float value; //initial value
    /*0x0C*/ float unValue; //unused value
    /*0x10*/ uint32 flag; //seems unused, 0
}; //used in SDR3 anim blocks, as REAL data type

// Size = 20 Byte / 0x14 byte
struct Aref_Colour
{
    /*0x00*/ AnimationReference AnimRef; //STC/STS reference
    /*0x08*/ uint8 value[4]; //b, g, r, alpha initial value 
    /*0x0C*/ uint8 unValue[4]; //unused value
    /*0x10*/ uint32 flags; //seems unused, 0
}; //used in SDCC anim blocks, as COL data type

typedef struct
{
    AnimationReference AnimRef;
    uint8 value[4]; //initial value
    uint8 unValue[4]; //unused extra value
    uint32 flags; //unused, 0
} Aref_fltByte4D;

struct Aref_Sphere
{
    AnimationReference AnimRef; //STC/STS reference
    Sphere value; //initial value
    Sphere unValue; //unused value
    uint32 flags; //seems unused, 0    
}; //used in SDMB anim blocks, as BNDS data type

// Size = 24 byte / 0x18 byte
// Complete
struct MD34
{ 
    /*0x00*/ char id[4]; 
    /*0x04*/ uint32 ofsRefs;
    /*0x08*/ uint32 nRefs;
	/*0x0C*/ Reference mref;
	/*0x18*/ char padding[8];
};

// vertFlags
#define	VERTFLAGS_VERT32		0x020000
#define	VERTFLAGS_VERT36		0x040000
#define	VERTFLAGS_VERT40		0x080000
#define	VERTFLAGS_VERT44		0x100000
// Size = 784 byte / 0x310 byte
struct MODL
{
	/*0x00*/ Reference name;
	/*0x0C*/ uint32 type;
	/*0x10*/ Reference mSEQS;		// sequenceHeader
	/*0x1C*/ Reference mSTC;			// sequenceData
	/*0x28*/ Reference mSTG;			// sequenceLookup
	/*0x34*/ Vec3D v3d1;
	/*0x40*/ uint32 d10;
	/*0x44*/ Reference mSTS;
	/*0x50*/ Reference mBone;
	/*0x5C*/ uint32 nSkinnedBones;
	/*0x60*/ uint32 vertFlags;
	/*0x64*/ Reference mVert;		// vertexData
	/*0x70*/ Reference mDIV;			// views
	/*0x7C*/ Reference mBoneLU;		// boneLookup
	/*0x88*/ SphereF boundSphere;
	/*0xA8*/ int d2[15];
	/*0xE4*/ Reference mAttach;
	/*0xF0*/ Reference mAttachLU;	// attachLookup
	/*0xFC*/ Reference mLite;		// Lights
	/*0x108*/ Reference mSHBX;
	/*0x114*/ Reference mCam;			// Camera
	/*0x120*/ Reference D;
	/*0x12C*/ Reference mMatLU;		// materialLookup
	/*0x138*/ Reference mMat;			// material
	/*0x144*/ Reference mDIS;		// Displacement
	/*0x150*/ Reference mCMP;		// Composite
	/*0x15C*/ Reference mTER;		// Terrain
	/*0x168*/ Reference mVOL;
	/*0x174*/ Reference r1;
	/*0x180*/ Reference mCREP;
	/*0x18C*/ Reference mPar;
	/*0x198*/ Reference mParc;
	/*0x1A4*/ Reference mRibbon;
	/*0x1B0*/ Reference mPROJ;
	/*0x1BC*/ Reference mFOR;
	/*0x1C8*/ Reference mWRP;
	/*0x1D4*/ Reference r2;
	/*0x1E0*/ Reference mPHRB;
	/*0x1EC*/ Reference r3[3];
	/*0x210*/ Reference mIKJT;
	/*0x21C*/ Reference r4;
	/*0x228*/ Reference mPATU;
	/*0x234*/ Reference mTRGD;
	/*0x240*/ Reference mIREF;
	/*0x24C*/ int d7[2];
	/*0x254*/ MatrixF mat;
	/*0x298*/ SphereF ext2;
	/*0x2B8*/ Reference mSGSS;
	/*0x2C4*/ Reference mATVL;
	/*0x2D0*/ Reference F;
	/*0x2DC*/ Reference G;
	/*0x2E8*/ Reference mBBSC;
	/*0x2F4*/ Reference mTMD;
	/*0x300*/ uint32 d9[4];

	void init()
	{
		type = 0x180d53;
		vertFlags = 0x182007D;
	}
};

#define	BONE_FLAGS_INHERIT_TRANSLATION	0x1
#define	BONE_FLAGS_INHERIT_SCALE		0x2
#define	BONE_FLAGS_INHERIT_ROTATION		0x4
#define	BONE_FLAGS_BILLBOARD1			0x10
#define	BONE_FLAGS_BILLBOARD2			0x40
#define	BONE_FLAGS_2D_PROJECTION		0x100
#define	BONE_FLAGS_ANIMATED				0x200
#define	BONE_FLAGS_INVERSE_KINEMATICS	0x400
#define	BONE_FLAGS_SKINNED				0x800
/*
The bones as defined in the .m3 files.

Bone Flags
Most flags have been determined through the previewer.

Bit Index	Bit Address	 Flag	 Description
1	 0x1	 Inherit translation	 Inherit translation from parent bone
2	 0x2	 Inherit scale	 Inherit scale from parent bone
3	 0x4	 Inherit rotation	 Inherit rotation from parent bone
5	 0x10	 Billboard (1)	 Billboard bone
7	 0x40	 Billboard (2)	 Billboard bone (again)
9	 0x100	 2D Projection	 Projects the bone in 2D?
10	 0x200	 Animated	 Bone has STC (transform) data. Will ignore STC data if set to false
11	 0x400	 Inverse Kinematics	 Grants the bone IK properties in the game engine
12	 0x800	 Skinned	 Bone has associated geometry. Necessary for rendering geometry weighted to the bone
14	 0x2000	 Unknown	 Always set to true in Blizzard models. No apparent effect

Extra Bone Information
Bone Configuration
M3 bones have two initial configurations that are in two different formats. The 
bindpose is first setup using bone matrices which can be found in the IREF 
chunks. Secondly, once the mesh is weighted to the bones, the initial position, 
rotation and scale values are applied to a bone in relation to the parent bone 
if the bone has one. In 3ds Max to apply these transformations appropriately, 
you have to begin at the deepest bones first and work your way up. This places 
the model in what I've decided to call the base pose. It's unclear what the 
purpose of the base pose is, but it seems to have relevance to how the model 
behaves in the absence of sequence transform data. This is different to WoW's M2 
model format which had pivot points for their bones to setup the bindpose and 
lacked any kind of base pose.

Some of Blizzards models use a terrible feature of 3ds Max that allows you to 
mirror bones by using negative scales. Examples of this can be found in 
Immortal.m3 and DarkTemplar.m3 aswell as I'm sure other models where the 
original artist has decided to mirror the bones through this tool. This can 
cause serious problems when correctly trying to animate bones outside of the 
Starcraft engine. In 3ds Max this is particularly a problem as rotations do 
not factor in the negative scaling resulting in improper orientation of bones 
during animations. A workaround is to force the bones to use a transform matrix 
with the negative scaling applied for each rotation keyframe and remove the 
extra keyframes generated in the scale and translation controllers.

Animation References
M3's use an odd system for referencing their animation data. There is a global 
list of all animation data represented in the STC structures for each animation 
sequence. AnimationReferences found in bones contain animflags of 6 when they 
contain data for at least one of the animations. Regardless of whether they 
contain data, they will always have a unique uint32 ID which I suppose is 
randomly generated. These ID's are referenced in MODL.STC.animid list if there 
is animation data present for that particular animation type in that particular 
animation sequence, if they are not referenced it indicates there is null 
animation data for that animation type.

In order to gather the correct animation data from the reference, you must 
iterate through the STC.animid list of uint32 ID's and find the matching ID in 
the list. Once you find that index, you check the same index in the next section 
of STC.animindex which provides the sequence data index and the index into the 
sequence data array present in STC.

As an example (all values are arbitrary), if you're trying to find rotation data 
in a bone, you grab the rot.AnimRef.animid from the bone. You go to the animation 
sequence data you want to animate this bone with, say MODL.STC[2], iterate through 
STC[2].animid until you find the animid in the list and record its index as 6. You 
go to STC[2].animind[6] and record the STC[2].animind[6].aind as 8 and 
STC[2].animind[6].sdind as 3. You'll find the appropriate sequence data for that 
bone in the MODL.STC[2].SeqData[3].SD4Q[8] SD (Sequence Data) chunk and can animate 
the bone accordingly. I hope this clarifies how animation data is referenced in the 
M3 file format.
*/
// Size = 160 byte / 0xA0 byte
// Incomplete
struct BONE
{
    /*0x00*/ int32 d1; // Keybone?
    /*0x04*/ Reference name; // bonename
    /*0x10*/ uint32 flags; //2560 = Weighted vertices rendered, 512 = not rendered
    /*0x14*/ int16 parent; // boneparent
    /*0x16*/ uint16 s1; // always 0
    /*0x1A*/ Aref_VEC3D initTrans; //bone position is relative to parent bone and its rotation
    /*0x3C*/ Aref_VEC4D initRot; //initial bone rotation
    /*0x68*/ Aref_VEC3D initScale; //initial scale
    /*0x90*/ Aref_UINT32 ar1;

	void init()
	{
		d1 = -1;
		flags = BONE_FLAGS_ANIMATED | BONE_FLAGS_SKINNED;
		initRot.value = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
		initRot.unValue = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
		initScale.value = Vec3D(1.0f, 1.0f, 1.0f);
		initScale.unValue = Vec3D(1.0f, 1.0f, 1.0f);
		ar1.value = 1;
		ar1.unValue = 1;
	}
};

/*
MATM is a material lookup table referenced by BAT structures in the DIV 
definition. It functions as a reference to the type and the index of a 
particular type of material. More information on materials can be found 
in the material definitions documentation.
*/
// Size = 8 byte / 0x08 byte
// Complete
#define	MATM_TYPE_STANDARD		1	// MAT
#define	MATM_TYPE_DISPLACEMENT	2	// DIS
#define	MATM_TYPE_COMPOSITE		3	// CMP
#define	MATM_TYPE_TERRIAN		4	// TER
#define	MATM_TYPE_VOL			5	// VOL
struct MATM
{
    /*0x00*/ uint32 matType; //see MATM_TYPE_*
    /*0x04*/ uint32 matind; //index into matType array

	void init()
	{
		matType = MATM_TYPE_STANDARD;
	}
};

/*
Material definitions for the M3 model. Materials act as a container for a group 
of bitmaps such as diffuse, specular, emissive, etc. They have general 
properties that play a role in how the bitmaps are ultimately rendered on the 
model. MATM are referenced in the DIV BAT matid's and function as a lookup table 
into these definitions.

Layers
Each layer index corresponds to a specific type of map. Some don't appear to do 
anything, even the previewer doesn't disclose their purpose. It's been found that 
some of the maps don't need to point to a real map to still have an effect on the 
material, such as Alpha Map properties when the alpha blend modes are used. More 
on that in the LAYR definition. Each LAYR must have a referenced chunk even if it's 
blank or else the previewer will crash.

Layer Index	Map Type
0	 Diffuse
1	 Decal
2	 Specular
3	 Emissive (1)
4	 Emissive (2)
5    Reflect
6	 Envio (Reflective)
7	 Alpha
8	 Alpha Mask
10	 Normal
11	 Height
*/
#define	MAT_LAYER_DIFF			(0)
#define	MAT_LAYER_EMISSIVE		(3)
#define	MAT_LAYER_ALPHA			(7)
/*
Flags
Most of these determined through the previewer.
*/
#define MAT_FLAG_UNFOGGED		(0x4)
#define MAT_FLAG_TWOSIDED		(0x8)
#define MAT_FLAG_UNSHADED		(0x10)
#define MAT_FLAG_NOSHADOWSCAST	(0x20)
#define MAT_FLAG_NOHITTEST		(0x40)
#define MAT_FLAG_NOSHADOWSRECEIVED	(0x80)
#define MAT_FLAG_DEPTHPREPASS	(0x100)
#define MAT_FLAG_USETERRAINHDR	(0x200)
#define MAT_FLAG_SPLATUVFIX		(0x800)
#define MAT_FLAG_SOFTBLENDING	(0x1000)
#define MAT_FLAG_UNFOGGED2		(0x2000)
/*
Blendmodes
Value	Mode
*/
#define	MAT_BLEND_OPAQUE		(0)
#define	MAT_BLEND_ALPHABLEND	(1)
#define	MAT_BLEND_ADD			(2)
#define	MAT_BLEND_ALPHAADD		(3)
#define	MAT_BLEND_MOD			(4)
#define	MAT_BLEND_MOD2x			(5)
/*
Layer/Emissive Blendmodes
Value	Mode
0	 Mod
1	 Mod 2x
2	 Add
3	 Blend
4	 Team Colour Emissive Add
5	 Team Colour Diffuse Add

Specular Types
Value	Type
0	 RGB
1	 Alpha Only
*/
// Size = 268 bytes / 0x10C bytes
// Incomplete
struct MAT
{
    Reference name;
    uint32 d1;
    uint32 flags;
    uint32 blendMode;
    uint32 priority;
    uint32 d2;
    float specularity;
    float f1;
    uint32 cutoutThresh;
    float SpecMult;
    float EmisMult;
    Reference layers[13];
    uint32 d4;
    uint32 layerBlend;
    uint32 emisBlend;
    uint32 d5; //controls emissive blending in someway, should be set to 2
    uint32 specType;
    Aref_UINT32 ar1;
    Aref_UINT32 ar2;

	void init()
	{
		SpecMult = 1;
		EmisMult = 1;
		layerBlend = 2;
		emisBlend = 2;
		d5 = 2;
	}
};

// Displacement Material
// Size = 68 bytes / 0x44 bytes
// Incomplete
struct DIS
{
    /*0x00*/ Reference name;
    /*0x0C*/ uint32 d1;
    /*0x10*/ Aref_FLOAT ar1;
    /*0x24*/ Reference normalMap; //LAYR ref
    /*0x30*/ Reference strengthMap; //LAYR ref
    /*0x3C*/ uint32 flags; //see material flags
    /*0x40*/ uint32 priority;
};

// Composite Material Reference
// Size = 24 bytes / 0x18 bytes
// Incomplete
struct CMS
{
    /*0x00*/ uint32 matmIndex;
    /*0x04*/ Aref_FLOAT ar1; //blend amount?
};

// Composite Material
// Size = 12 bytes / 0x0C bytes
// Incomplete
struct CMP
{
    /*0x00*/ Reference name;
    /*0x0C*/ uint32 d1;
    /*0x10*/ Reference compositeMats; //CMS ref
};

// Terrain (Null) Material
// Size = 24 bytes / 0x18 bytes
// Complete
struct TER
{
    /*0x00*/ Reference name;
    /*0x0C*/ Reference nullMap; //usually blank LAYR ref
};

/*
Bitmaps are referenced through LAYR definitions which are found referenced in 
the materials.

LAYR's can contain a path to an internal bitmap file which is usually a DDS 
file or in the case of animated textures an OGV file. Even if they do not 
contain a map path, they may still influence how a material is rendered. For 
example, alpha map LAYR settings can influence the way standard materials are 
rendered when alpha blend modes are used. More material information can be 
found in the material definitions documentation.

The previewer contains no information related to bitmaps except the bitmap 
filename if it is present. This has made documenting the LAYR structure 
accurately problematic.
*/
#define	LAYR_FLAGS_TEXWRAP_X			(0x4)
#define	LAYR_FLAGS_TEXWRAP_Y			(0x8)
#define	LAYR_FLAGS_TEXBLACK				(0x10)
#define	LAYR_FLAGS_SPLIT				(0x100)
#define	LAYR_FLAGS_COLOR				(0x400)

#define	LAYR_ALPHAFLAGS_ALPHATEAMCOLOR	(0x1)
#define	LAYR_ALPHAFLAGS_ALPHAONLY		(0x2)
#define	LAYR_ALPHAFLAGS_ALPHASHADING	(0x4)
#define	LAYR_ALPHAFLAGS_TEXGARBLE		(0x8)
#define	LAYR_TINEFLAGS_INVERSETINT		(0x1)
#define	LAYR_TINEFLAGS_TINT				(0x2)
#define	LAYR_TINEFLAGS_USECOLOR			(0x4)
// Size = 356 bytes / 0x164 bytes
// Incomplete
struct LAYR
{
    uint32 d1;
    Reference name;
	Aref_Colour Colour;
	uint32 flags; // LAYR_FLAGS_*
	uint32 uvmapChannel;
	uint32 alphaFlags; // LAYR_ALPHAFLAGS_*
	Aref_FLOAT brightness_mult1;
	Aref_FLOAT brightness_mult2;
	uint32 d4;
	long d5; //should be set to -1 or else texture not rendered
	uint32 d6[2];
	int32 d7; //set to -1 in animated bitmaps
	uint32 d8[2];
	Aref_UINT32 ar1;
	Aref_VEC2D ar2;
	Aref_UINT16 ar3;
	Aref_VEC2D ar4;
	Aref_VEC3D uvAngle; //3dsAngle = value * 50 * -1
	Aref_VEC2D uvTiling;
	Aref_UINT32 ar5;
	Aref_FLOAT ar6;
	Aref_FLOAT brightness; //0.0 to 1.0 only?
	int32 d20; //seems to affect UV coords? should be set to -1
	uint32 tintFlags; // LAYR_TINEFLAGS_*
	float tintStrength; //set to 4 by default in Blizzard models
	float tintUnk; //0.0 to 1.0 only?
	float tintUnk2[2]; //seems to be more settings for tint

	void init()
	{
		flags = 236; // 0xEC, C=TEXWRAP_X|TEXWRAP_Y
		d5 = -1;
		brightness_mult1.value = 1.0f;
		brightness_mult1.unValue = 1.0f;
		uvTiling.value = Vec2D(1.0f, 1.0f);
		d20 = -1;
	}
};

// Size = 52 byte / 0x34 byte
// Incomplete
struct DIV
{
    /*0x00*/ Reference faces;
    /*0x0C*/ Reference REGN;	// submesh
    /*0x18*/ Reference BAT;
    /*0x24*/ Reference MSEC;
    /*0x30*/ uint32 unk; //always 1?

	void init()
	{
		unk = 1;
	}
};

// Size = 14 byte / 0x0E byte
// Incomplete
#pragma pack(push,1) 
struct BAT
{
    /*0x00*/ uint32 d1;
    /*0x04*/ uint16 regnIndex; //REGN index
    /*0x06*/ uint16 s1[2];
    /*0x0A*/ uint16 matmIndex; //MATM index (MATM is a material lookup table)
    /*0x0C*/ int16 s2; //usually -1

	void init()
	{
		s2 = -1;
	}
};
#pragma pack(pop) 

//Size = 36 byte / 0x24 byte
// Incomplete
struct REGN
{
    /*0x00*/ uint32 d1[2]; //always 0?
    /*0x08*/ uint32 indVert;
    /*0x0C*/ uint32 numVert;
    /*0x10*/ uint32 indFaces;
    /*0x14*/ uint32 numFaces;
    /*0x18*/ uint16 boneCount; //boneLookup total count (redundant?)
    /*0x1A*/ uint16 indBone; //indice into boneLookup
    /*0x1C*/ uint16 numBone; //number of bones used from boneLookup
    /*0x1E*/ uint16 s2; //flags? vital for sc2 engine rendering the geometry
    /*0x20*/ unsigned char b1[2];
    /*0x22*/ uint16 rootBone;

	void init()
	{
		b1[0] = b1[1] = 1;
	}
};

// Size = 72 byte / 0x48 byte
// Incomplete
struct MSEC
{
    /*0x00*/ uint32 d1; //always 0?
    /*0x04*/ Aref_Sphere bndSphere;
};

// Size = 176 byte / 0xB0 byte
// Incomplete
struct CAM
{
    /*0x00*/ int32 d1;
    /*0x04*/ Reference name;
    /*0x10*/ uint16 flags1;
    /*0x12*/ uint16 flags2;
};

// Size = 388 byte / 0x184 byte
// Incomplete
struct PROJ
{
};

/*
Event definitions as defined in the .m3 files and found referenced in the STC 
definitions. It contains something that looks like a matrix, though I am not 
sure what it is used for.

A default event is present for every sequence with the name 'Evt_SeqEnd' that 
generally fires on the last frame of the sequence. This event is mainly used 
in sequences that loop. It indicates at which frame the sequence should end 
and when the next sequence should begin in the loop. I believe this has been 
implemented to allow greater control of how the sequences flow in the loop as 
opposed to how they play out when called alone. If these events are absent, 
looped sequences that are called will play one of the sequences in the loop 
and freeze on the last frame without continuing through the loop. These events 
must be generated on export if you intend to have the model sequences loop 
properly within the game engine.
*/
// Size = 104 byte/ 0x68 byte
// Incomplete
struct EVNT
{
    /*0x00*/ Reference name;
    /*0x0C*/ int32 d1;
    /*0x10*/ int16 s1;
    /*0x12*/ uint16 s2;
    /*0x14*/ Vec4D a;
	/*0x24*/ Vec4D b;
	/*0x34*/ Vec4D c;
	/*0x44*/ Vec4D d;
	/*0x54*/ int32 d2[5];

	void init()
	{
		d1 = -1;
		s1 = -1;
		a = Vec4D(1.0f, 0.0f, 0.0f, 0.0f);
		b = Vec4D(0.0f, 1.0f, 0.0f, 0.0f);
		c = Vec4D(0.0f, 0.0f, 1.0f, 0.0f);
		d = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
		d2[0] = 4;
	}
};

// Size = 20 byte / 0x14 byte
// Incomplete
struct ATT
{
    /*0x00*/ int32 i1; // -1
    /*0x04*/ Reference name;
    /*0x10*/ int32 bone;

	void init()
	{
		i1 = -1;
	}
};

// Size = 32 byte / 0x20 byte
// Complete, Animationblock, SD3V, SD4Q
struct SD
{
    /*0x00*/ Reference timeline;
    /*0x0C*/ uint32 flags;
    /*0x10*/ uint32 length;
    /*0x14*/ Reference data;

	void init()
	{
		flags = 1;
	}
};

/*
Header for animation sequences defined in the .m3 files.

Main header for the various sequences of a model. The STG chunk contains the 
same amount of indices as this block and functions as a lookup table that 
connects STC transformation data to a particular SEQS entry. Some models 
(Marine.m3) have multiple STC blocks for a single SEQS entry, the purpose 
of which is poorly understood.

Sequence Flags
Bit Index	Bit Address	 Flag	 Description
1	 0x1	 Non-Looping	 If set true, sequence is non-looping
2	 0x2	 Global Sequence (hard)	 Forces sequence to play regardless of other sequences
4	 0x8	 Global Sequence (previewer)	 Sets Global Sequence true in previewer but otherwise has no effect

The looping flag works inversely, so that if it's set to true it is non-looping. 
The hard global sequence flag forces the model to play the sequence endlessly 
and it would seem no other sequence can affect the model. The previewer global 
sequence flag sets the Global Animation boolean to true in the previewer but 
seems to have no apparent affect on the model. Further testing with the global 
flags is required.

Sequence ID's
Names function as an ID within the engine to call certain sequences in response 
to particular game events (Walk when moving, Stand when idle, Attack when 
attacking, etc). Multiple sequences for the same sequence ID need to have 
numbers appended after a space to avoid sequence calling conflicts (i.e. Stand 
01, Attack 03).

Frequency
Frequency determines the probability a sequence will be called. It's only relevant 
if multiple sequences are defined for the same sequence ID. What's interesting is 
that this is a uint32 value and not a float. From what I have observed, the values 
function as a ratio in comparison to the other sequence frequencies in the group. 
This is different from the WoW format which used percentages. For example, say 
Attack 01 has a frequency of 3 while Attack 02 has a frequency of 1. I believe 
this means Attack 01 is three times more likely to be called than Attack 02. 
Frequency values can then be arbitrary however in Blizzard models it is common to 
see either values around 1 or values around 100.

Start and End frames
Start and end frames can be set for sequences that determine ultimately at what 
frame the sequence begins and ends along the sequences timeline. Almost all 
Blizzard models I have looked at start at frame 0 and end at the final frame 
of the sequence.

There is additional Start and End frames for a Replay setting (seen in the 
previewer). I have not done any testing but I assume that this is used to play 
a different timeframe range when the sequence has been called multiple times 
within a loop. Most Blizzard models I have looked at set their replay start 
and end frames to 1, which results in a length of 0 indicated in the previewer. 
I believe if the length is 0, the animation will be played from start to finish 
as normal regardless of it being looped. I may follow up with further tests to 
see if this is correct.
*/
// Size = 96 byte / 0x60 byte
// Incomplete
struct SEQS
{
    /*0x00*/ int32 d1[2]; //usually -1
    /*0x08*/ Reference name;
    /*0x14*/ int32 animStart;
    /*0x18*/ int32 length;			// animLength
    /*0x1C*/ float moveSpeed;		// used in movement sequences (Walk, Run)
    /*0x20*/ uint32 flags;
    /*0x24*/ uint32 frequency;		// how often it plays
    /*0x28*/ uint32 ReplayStart;	// in most Blizzard models set to 1
    /*0x2C*/ uint32 ReplayEnd;		// in most Blizzard models set to 1
    /*0x2C*/ int32 d4[2];			// usually 0
    /*0x38*/ SphereF boundSphere;
    /*0x58*/ int32 d5[2];			// usually 0

	void init()
	{
		d1[0] = -1;
		d1[1] = -1;
		ReplayStart = 1;
		ReplayEnd = 1;
		d4[0] = 0x64;
	}
};

// Size = 4 byte / 0x04 byte
// Incomplete
struct AnimationIndex
{
    /*0x00*/ uint16 aind; //anim ind in seq data
    /*0x02*/ uint16 sdind; //seq data array index
};

// Size = 204 byte / 0xCC byte
// Incomplete
/*
These blocks works like some sort of headers for sequences, referencing Sequence Data 
blocks and some other stuff.

Seems there can be more than one STC block per sequence if there are several versions 
of an animation sequence. STG represents an STC lookup table for each SEQS entry. 
Example: Cover Full, Cover Shield

Bones, among other structures, reference the Sequence Data located in these structs for animation.

Sequence Data Types
Known Sequence Data types:

SD ID	Index	 Type	 Data ID	 Found In	 Description
SDEV	 0	Event	EVNT	 Unknown	 Event Animation?
SD2V	 1	Vector 2D	VEC2	 PAR	 Unknown
SD3V	 2	Vector 3D	VEC3	BONE	 Translation/Scale
SD4Q	 3	Quaternion	QUAT	BONE	 Rotation
SDCC	 4	Colour (4 byte floats)	COL	 RIB	 Colour (Blue, Green, Red, Alpha)
SDR3	 5	Float	REAL	 PAR	 Unknown
SDS6	 7	int16	 I16	 Unknown	 Unknown
SDFG	 11	int32	FLAG	 RIB	 Flags?
SDMB	 12	Extent	BNDS	MSEC	 Bounding Sphere

Sequence Data Information
Translation and Scaling animation data use the same Sequence Data entry, SD3V 
which uses VEC3's for keyframes. EVNT animations determine the end of an 
animation sequence. Sequences that are part of a looped series require 
'Evt_SeqEnd' EVNT keyframes so that they only loop once in the sequence. 
Examples of these animations are Stand animations, which typically all loop, 
but each only plays once in the loop and are picked based on their frequency 
setting of their corresponding SEQS entry.

Animation References
Animation references indirectly reference the data found in the Sequence Data 
arrays. See the BONE animation data description to get a handle on how STC 
data is referenced using animation references. I believe there will be a total 
of 13 different types of animation references, one for each index of the 
sequence data array. So far only some have been discovered within M3 files.
*/
#define	STC_INDEX_EVENT    (0)
#define	STC_INDEX_VEC2D    (1)
#define	STC_INDEX_VEC3D    (2)
#define	STC_INDEX_QUAT     (3)
#define	STC_INDEX_COLOR    (4)
#define	STC_INDEX_FLOAT    (5)
#define	STC_INDEX_INT16    (7)

struct STC
{
    /*0x00*/ Reference name;
    /*0x0C*/ uint32 d1;
    /*0x10*/ uint16 indSEQ[2]; //points to animation in SEQS chunk, twice for some reason
    /*0x14*/ Reference animid; //list of unique uint32s used in chunks with animation. The index of these correspond with the data in the next reference.
    /*0x20*/ Reference animindex; //lookup table, connects animid with it's animation data, nEntries of AnimationIndex reference using U32_ id
    /*0x2C*/ uint32 d2;
    /*0x30*/ Reference Events; // EVNT
    /*0x3C*/ Reference arVec2D; // SD2V
    /*0x48*/ Reference arVec3D; // SD3V - Trans, Scale
    /*0x54*/ Reference arQuat; // SD4Q - Rotation
    /*0x60*/ Reference arColour;
    /*0x6C*/ Reference arFloat; // SDR3
    /*0x78*/ Reference r3;
    /*0x84*/ Reference arInt16; // SDS6
    /*0x90*/ Reference r4;
    /*0x9C*/ Reference r5;
    /*0xA8*/ Reference r6;
    /*0xB4*/ Reference arFlags; // SDFG - Flags
    /*0xC0*/ Reference arBounds; // SDMB - Bounding Boxes?
};

// Size = 28 byte / 0x1C byte
// Incomplete
struct STS
{
    /*0x00*/ Reference animid;	// uint32 values, same as what's in STC?
    /*0x0C*/ int32 d1[3];		// usually -1
    /*0x18*/ int16 s1;			// usually -1
    /*0x1A*/ uint16 s2;			// usually 0

	void init()
	{
		d1[0] = -1;
		d1[1] = -1;
		d1[2] = -1;
		s1 = -1;
	}
};

// Size = 24 byte / 0x18 byte
// Complete
/*
The number of STG blocks in a .m3 file equals the number of SEQS blocks, and tells 
where in the STC list the animations of the corresponding SEQS block starts.
*/
struct STG
{
    /*0x00*/ Reference name;
    /*0x0C*/ Reference stcID;
};

// Size = 28 byte / 0x1C byte
// Incomplete
struct BNDS
{
    /*0x00*/ Sphere bndSphere;
};

// Size = 64 byte / 0x40 byte
// Complete
struct IREF
{
    float matrix[4][4];

	void init()
	{
		matrix[0][1] = 1.0f;
		matrix[1][0] = -1.0f;
		matrix[2][2] = 1.0f;
		matrix[3][3] = 1.0f;
	}
};

#define PARTICLEFLAG_sort                  0x1;
#define PARTICLEFLAG_collideTerrain        0x2;
#define PARTICLEFLAG_collideObjects        0x4;
#define PARTICLEFLAG_spawnOnBounce         0x8;
#define PARTICLEFLAG_useInnerShape         0x10;
#define PARTICLEFLAG_inheritEmissionParams 0x20;
#define PARTICLEFLAG_inheritParentVel      0x40;
#define PARTICLEFLAG_sortByZHeight         0x80;
#define PARTICLEFLAG_reverseIteration      0x100;
#define PARTICLEFLAG_smoothRotation        0x200;
#define PARTICLEFLAG_bezSmoothRotation     0x400;
#define PARTICLEFLAG_smoothSize            0x800;
#define PARTICLEFLAG_bezSmoothSize         0x1000;
#define PARTICLEFLAG_smoothColour          0x2000;
#define PARTICLEFLAG_bezSmoothColour       0x4000;
#define PARTICLEFLAG_litParts              0x8000;
#define PARTICLEFLAG_randFlipbookStart     0x10000;
#define PARTICLEFLAG_multiplyByGravity     0x20000;
#define PARTICLEFLAG_clampTailParts        0x40000;
#define PARTICLEFLAG_spawnTrailingParts    0x80000;
#define PARTICLEFLAG_fixLengthTailParts    0x100000;
#define PARTICLEFLAG_useVertexAlpha        0x200000;
#define PARTICLEFLAG_modelParts            0x400000;
#define PARTICLEFLAG_swapYZonModelParts    0x800000;
#define PARTICLEFLAG_scaleTimeByParent     0x1000000;
#define PARTICLEFLAG_useLocalTime          0x2000000;
#define PARTICLEFLAG_simulateOnInit        0x4000000;
#define PARTICLEFLAG_copy                  0x8000000;

struct PAR
{
	uint32 bone;
	uint32 matmIndex;
	Aref_FLOAT emisSpeedStart;
    Aref_FLOAT speedVariation;
    uint32 enableSpeedVariation; //1 - On, 0 - Off
    Aref_FLOAT yAngle;
    Aref_FLOAT xAngle;
    Aref_FLOAT xSpread;
    Aref_FLOAT ySpread;
    Aref_FLOAT lifespan;
    Aref_FLOAT decay;
    uint32 enableDecay; //1 - On, 0 - Off
    uint32 d9[3];
    float emisSpeedMid; //change in emission speed
    float scaleRatio; //0 - 1.0, impacts on scale range
    float f5[3];
    Aref_VEC3D scale1; //[start, mid, end]
    Aref_VEC3D speedUnk1;
    //main particle colour attributes
    //struct
    //{
        Aref_fltByte4D col1Start;
        Aref_fltByte4D col1Mid;
        Aref_fltByte4D col1End;
    //} Colour1StartMidEnd;
    float emisSpeedEnd; //odd setting
    uint32 d16;
    float f9[2];
    uint32 d17[2];
    uint8 b4[4];
    uint8 b5[4];
    uint32 d18[3];
    float f10;
    uint32 d19; //possibly minimum particles? not sure
    uint32 maxParticles; //maximum amount of particles
    Aref_FLOAT emissionRate; //rate at which particles are released till maxParticles
    //particle types determined through beta previewer
	uint32 ptenum;
    //enum <uint32> ptenum { ptPoint, ptPlane, ptSphere, ptBox, ptCylinder1, ptDisc, ptSpline, ptPlanarBillboard, ptPlanar, ptCylinder2, ptStarshaped   } particleType;
    Aref_VEC3D emissionArea; //[width, length, spread]
    Aref_VEC3D tailUnk1;
    Aref_FLOAT pivotSpread;
    Aref_VEC2D spreadUnk1;
    Aref_VEC3D ar19;
    uint32 enableRotate; //1 - On, 0 - Off
    Aref_VEC3D rotate; //[rotSpread1, rotSpread2, spinSpeed]
    uint32 enableColour2; //1 - On, 0 - Off
    //if enabled, seems to pick randomly between Colour1 and Colour2 values
    //struct
    //{
        Aref_fltByte4D col2Start;
        Aref_fltByte4D col2Mid;
        Aref_fltByte4D col2End;
    //} Colour2StartMidEnd;
    uint32 d23;
    Aref_UINT16 ar24;
    unsigned char col1[4]; // flyByte b, g, r, a
    float lifespanRatio;
    uint16 columns;
    uint16 rows;
    float f15[2];
    uint32 d25;
    float f12;
    long i1; //must be -1 or will crash the previewer
    uint32 d26[6];
    float f13;
    uint32 d27[2];
    float f14;
    uint32 d28;

    /*
    Following collection of Aref's appear to be completely unused

    Have found no models that use them and altering their values seems to have
    no effect
    
    Seem to have a new Aref type, Short Vec3D's, but that's only an assumption
    as the amount of bytes between Aref's fits, must find a model that uses these...
    */
    
    Aref_UINT32 ar25;
    Aref_shrtVEC3D ar26;
    Aref_UINT32 ar27;
    Aref_shrtVEC3D ar28;
    Aref_UINT32 ar29;
    Aref_shrtVEC3D ar30;
    Aref_UINT32 ar31;
    Aref_shrtVEC3D ar32;
    Aref_UINT32 ar33;
    Aref_shrtVEC3D ar34;
    Aref_UINT32 ar35;
    Aref_shrtVEC3D ar36;
    Aref_UINT32 ar37;
    Aref_shrtVEC3D ar38;
    Aref_UINT32 ar39;
    Aref_shrtVEC3D ar40;
    Aref_UINT32 ar41;
    Aref_UINT32 ar42;
    Aref_UINT32 ar43;
    uint32 parFlags; //particleFlags, determined through beta previewer
    uint32 d29[6];
    Aref_UINT32 ar44;
    Aref_FLOAT ar45;
    long i2; //must be -1
    uint32 d30;
    Aref_UINT32 ar46;
    unsigned char col4[4]; // flyByte b, g, r, a
    uint32 unk[7];
};

#define	RIBBON_TYPE_PLANARBILLBOARDED	0
#define	RIBBON_TYPE_PLANAR				1
#define	RIBBON_TYPE_CYLINDER			2
#define	RIBBON_TYPE_STARSHAPED			3
#define	RIBBON_FLAGS_collideWithTerrain		0x2
#define	RIBBON_FLAGS_collideWithObjects		0x4
#define	RIBBON_FLAGS_edgeFalloff			0x8
#define	RIBBON_FLAGS_inheritParentVelocity	0x10
#define	RIBBON_FLAGS_smoothSize				0x20
#define	RIBBON_FLAGS_bezierSmoothSize		0x40
#define	RIBBON_FLAGS_useVertexAlpha			0x80
#define	RIBBON_FLAGS_scaleTimeByParent		0x100
#define	RIBBON_FLAGS_forceLegacy			0x200
#define	RIBBON_FLAGS_useLocalTime			0x400
#define	RIBBON_FLAGS_simulateOnInit			0x800
#define	RIBBON_FLAGS_useLengthAndTime		0x1000
// Ribbons
struct RIB
{
    uint32 bone;
    uint32 matmIndex;
    Aref_FLOAT renderSpeed; //determines how fast ribbon renders, can affect ribbon end point?
    Aref_UINT32 ar2;
    uint32 d1;
    Aref_UINT32 ar3;
    Aref_UINT32 ar4;
    Aref_UINT32 ar5;
    Aref_UINT32 ar6;
    Aref_FLOAT ar7;
    Aref_VEC3D ar8;
    float emissionAngle; //determines where ribbon ends
    float ribThickness; //0 - 1.0
    float f1[3];
    Aref_VEC3D ribScale; //[start, mid, end]
    Aref_VEC3D ribRotate; //[rotSpread1, rotSpread2, unk]?
    Aref_Colour ar11;
    Aref_Colour ar12;
    Aref_Colour ar13;
    float f2; //affects render speed
    uint32 d2;
    float f3;
    uint32 d3[8];
    float f4;
    uint32 d4;
    //determined through previewer
    uint32 ribbonType; //enum <ulong> ribbonType { rtPlanarBillboarded, rtPlanar, rtCylinder, rtStarShaped    } ribType;
    uint32 d5;
    float emissionRate;
    uint32 d6;
    float f6;
    Aref_FLOAT ribLength;
    uint32 d7[4];
    Aref_UINT32 visible; //animated FLAG, seems to switch ribbon on or off
    uint32 ribFlags; //ribbonFlags ribFlags; //determined through beta previewer
    uint32 d8[5];
    Aref_UINT32 ar16;
    Aref_UINT32 ar17;
    uint32 d9;
    Aref_UINT32 ar18;
    Aref_UINT32 ar19;
    uint32 d10;
    Aref_UINT32 ar20;
    Aref_UINT32 ar21;
    uint32 d11;
    Aref_UINT32 ar22;
    Aref_UINT32 ar23;
    uint32 d12;
    Aref_UINT32 ar24;
    Aref_UINT32 ar25;
    Aref_FLOAT ar26;
    Aref_UINT32 ar27;
};

/*
In the .m3 files, the vertex data seems to be contained within a uint8 block.

MODL-> flags defines some vertex stuff:

0x20000 = has vertices
0x40000 = has 1 extra UV coords
0x80000 = has 2 extra UV coords
0x100000 = has 3 extra UV coords

Each UV texture coordinate must be divided by 2046.0 to get its true float value. In 3ds 
max, the Y-UV coord must be flipped (1 - UV.y) for textures to be displayed on the mesh 
properly.

Vertex Weighting

Each vertex boneIndex is not an index into the global bone entries found in MODL but 
rather a reference into the bonelookup. However, it's not just an index into the 
bonelookup entries either. In order to find the correct bone to weight the vertice 
to, the boneIndex value uses submesh information found in the REGN indBone value to 
grab the right bone in the bonelookup entries. So in order to calculate the correct 
boneIndex:

1. Find which REGN entry the vertex belongs to
2. Add the REGN.indBone to the vertex.boneIndex value
3. Grab the bonelookup value your new index points to
4. Get the bone the bonelookup value refers to
*/
struct Vertex32 // 32 byte
{
    /*0x00*/ Vec3D pos;
    /*0x0C*/ char weBone[4]; // fltByte
    /*0x10*/ unsigned char weIndice[4]; //index in boneLookup of vertex submesh
    /*0x14*/ char normal[4];  // fltNormal, x, y, z, w (w is the scale)
    /*0x18*/ uint16 uv[2]; // uvShort
    /*0x1C*/ char tangents[4];
};

#if 0
struct Vertex36 // 36 byte
{
    Vec3D pos;
    char boneWeight[4];
    char boneIndex[4]; //index in boneLookup of vertex submesh
    char normal[4];  // x, y, z, w (w is the scale)
    uint16 uv1[2];
    uint16 uv2[2];
    char tangents[4];
};

struct Vertex40 // 40 byte
{
    Vec3D pos;
    char boneWeight[4];
    char boneIndex[4]; //index in boneLookup of vertex submesh
    char normal[4];  // x, y, z, w (w is the scale)
    uint16 uv1[2];
    uint16 uv2[2];
    uint16 uv3[2];
    char tangents[4];
};

struct Vertex44 // 44 byte
{
    Vec3D pos;
    char boneWeight[4];
    char boneIndex[4]; //index in boneLookup of vertex submesh
    char normal[4];  // x, y, z, w (w is the scale)
    uint16 uv1[2];
    uint16 uv2[2];
    uint16 uv3[2];
    uint16 uv4[2];
    char tangents[4];
};
#endif // 0

#endif
