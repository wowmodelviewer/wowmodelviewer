/*
 * wow_enums.h
 *
 *  Created on: 7 Aug. 2015
 *      Author: Jeromnimo
 */

#ifndef _WOW_ENUMS_H_
#define _WOW_ENUMS_H_

enum CharSlots {
	CS_HEAD,
	CS_NECK,
	CS_SHOULDER,
	CS_BOOTS,
	CS_BELT,
	CS_SHIRT,
	CS_PANTS,
	CS_CHEST,
	CS_BRACERS,
	CS_GLOVES,
	CS_HAND_RIGHT,
	CS_HAND_LEFT,
	CS_CAPE,
	CS_TABARD,
	CS_QUIVER,

	NUM_CHAR_SLOTS
};

enum CharGeosets {
	CG_HAIRSTYLE,
	CG_GEOSET100,
	CG_GEOSET200,
	CG_GEOSET300,
	CG_GLOVES,
	CG_BOOTS,		// 5
	CG_EARS = 7,
	CG_WRISTBANDS,
	CG_KNEEPADS,
	CG_PANTS,		// 10
	CG_PANTS2,
	CG_TARBARD,
	CG_TROUSERS,
	CG_TARBARD2,
	CG_CAPE,		// 15
	CG_EYEGLOW = 17,
	CG_BELT,
	CG_TAIL,
	CG_HDFEET,

	NUM_GEOSETS
};

#define	UPPER_BODY_BONES	5

enum POSITION_SLOTS { // wxString Attach_Names[]
	ATT_LEFT_WRIST = 0, // Mountpoint
	ATT_RIGHT_PALM,
	ATT_LEFT_PALM,
	ATT_RIGHT_ELBOW,
	ATT_LEFT_ELBOW,
	ATT_RIGHT_SHOULDER, // 5
	ATT_LEFT_SHOULDER,
	ATT_RIGHT_KNEE,
	ATT_LEFT_KNEE,
	ATT_RIGHT_HIP,
	ATT_LEFT_HIP, // 10
	ATT_HELMET,
	ATT_BACK,
	ATT_RIGHT_SHOULDER_HORIZONTAL,
	ATT_LEFT_SHOULDER_HORIZONTAL,
	ATT_BUST, // 15
	ATT_BUST2,
	ATT_FACE,
	ATT_ABOVE_CHARACTER,
	ATT_GROUND,
	ATT_TOP_OF_HEAD, // 20
	ATT_LEFT_PALM2,
	ATT_RIGHT_PALM2,
	ATT_PRE_CAST_2L,
	ATT_PRE_CAST_2R,
	ATT_PRE_CAST_3, // 25
	ATT_RIGHT_BACK_SHEATH,
	ATT_LEFT_BACK_SHEATH,
	ATT_MIDDLE_BACK_SHEATH,
	ATT_BELLY,
	ATT_LEFT_BACK, // 30
	ATT_RIGHT_BACK,
	ATT_LEFT_HIP_SHEATH,
	ATT_RIGHT_HIP_SHEATH,
	ATT_BUST3, // Spell Impact
	ATT_PALM3, // 35
	ATT_RIGHT_PALM_UNK2,
	ATT_DEMOLISHERVEHICLE,
	ATT_DEMOLISHERVEHICLE2,
	ATT_VEHICLE_SEAT1,
	ATT_VEHICLE_SEAT2, // 40
	ATT_VEHICLE_SEAT3,
	ATT_VEHICLE_SEAT4,
	ATT_VEHICLE_SEAT5,
	ATT_VEHICLE_SEAT6,
	ATT_VEHICLE_SEAT7, // 45
	ATT_VEHICLE_SEAT8,
	ATT_LEFT_FOOT,
	ATT_RIGHT_FOOT,
	ATT_SHIELD_NO_GLOVE,
	ATT_SPINELOW, // 50
	ATT_ALTERED_SHOULDER_R,
	ATT_ALTERED_SHOULDER_L,
	ATT_BELT_BUCKLE,
	ATT_SHEATH_CROSSBOW
};

enum CharRegions {
	CR_BASE = 0,
	CR_ARM_UPPER = 1,
	CR_ARM_LOWER = 2,
	CR_HAND = 3,
	CR_TORSO_UPPER = 4 ,
	CR_TORSO_LOWER = 5,
	CR_PELVIS_UPPER = 6,
	CR_PELVIS_LOWER = 7,
	CR_FOOT = 8,
	CR_UNK8 = 9,
	CR_FACE_UPPER = 10,
	CR_FACE_LOWER = 11,
	NUM_REGIONS,

	CR_LEG_UPPER = CR_PELVIS_UPPER,
	CR_LEG_LOWER = CR_PELVIS_LOWER,
	CR_CAPE = NUM_REGIONS+1,
	CR_TABARD_1,
	CR_TABARD_2,
	CR_TABARD_3,
	CR_TABARD_4,
	CR_TABARD_5,
	CR_TABARD_6
};

enum KeyBoneTable { // wxString Bone_Names[]
	//Block F - Key Bone lookup table.
	//---------------------------------
	BONE_LARM = 0,		// 0, ArmL: Left upper arm
	BONE_RARM,			// 1, ArmR: Right upper arm
	BONE_LSHOULDER,		// 2, ShoulderL: Left Shoulder / deltoid area
	BONE_RSHOULDER,		// 3, ShoulderR: Right Shoulder / deltoid area
	BONE_STOMACH,		// 4, SpineLow: (upper?) abdomen
	BONE_WAIST,			// 5, Waist: (lower abdomen?) waist
	BONE_HEAD,			// 6, Head
	BONE_JAW,			// 7, Jaw: jaw/mouth
	BONE_RFINGER1,		// 8, IndexFingerR: (Trolls have 3 "fingers", this points to the 2nd one.
	BONE_RFINGER2,		// 9, MiddleFingerR: center finger - only used by dwarfs.. don't know why
	BONE_RFINGER3,		// 10, PinkyFingerR: (Trolls have 3 "fingers", this points to the 3rd one.
	BONE_RFINGERS,		// 11, RingFingerR: Right fingers -- this is -1 for trolls, they have no fingers, only the 3 thumb like thingys
	BONE_RTHUMB,		// 12, ThumbR: Right Thumb
	BONE_LFINGER1,		// 13, IndexFingerL: (Trolls have 3 "fingers", this points to the 2nd one.
	BONE_LFINGER2,		// 14, MiddleFingerL: Center finger - only used by dwarfs.
	BONE_LFINGER3,		// 15, PinkyFingerL: (Trolls have 3 "fingers", this points to the 3rd one.
	BONE_LFINGERS,		// 16, RingFingerL: Left fingers
	BONE_LTHUMB,		// 17, ThubbL: Left Thumb
	BONE_BTH,			// 18, $BTH: In front of head
	BONE_CSR,			// 19, $CSR: Left hand
	BONE_CSL,			// 20, $CSL: Left hand
	BONE_BREATH,		// 21, _Breath
	BONE_NAME,			// 22, _Name
	BONE_NAMEMOUNT,		// 23, _NameMount
	BONE_CHD,			// 24, $CHD: Head
	BONE_CCH,			// 25, $CCH: Bust
	BONE_ROOT,			// 26, Root: The "Root" bone,  this controls rotations, transformations, etc of the whole model and all subsequent bones.
	BONE_WHEEL1,		// 27, Wheel1
	BONE_WHEEL2,		// 28, Wheel2
	BONE_WHEEL3,		// 29, Wheel3
	BONE_WHEEL4,		// 30, Wheel4
	BONE_WHEEL5,		// 31, Wheel5
	BONE_WHEEL6,		// 32, Wheel6
	BONE_WHEEL7,		// 33, Wheel7
	BONE_WHEEL8,		// 34, Wheel8
	BONE_MAX
};

enum ModelType {
	MT_NORMAL,
	MT_CHAR,
	MT_WMO,
	MT_NPC
};

enum SheathTypes
{
	SHEATHETYPE_NONE                   = 0,
	SHEATHETYPE_MAINHAND               = 1,
	SHEATHETYPE_LARGEWEAPON            = 2,
	SHEATHETYPE_HIPWEAPON              = 3,
	SHEATHETYPE_SHIELD                 = 4
};

// Item type values as referred to by the items.csv list
enum ItemTypes
{
	IT_ALL = 0,
	IT_HEAD,
	IT_NECK,
	IT_SHOULDER,
	IT_SHIRT,
	IT_CHEST,
	IT_BELT,
	IT_PANTS,
	IT_BOOTS,
	IT_BRACERS,
	IT_GLOVES,
	IT_RINGS,
	IT_ACCESSORY,
	IT_DAGGER,
	IT_SHIELD,
	IT_BOW,
	IT_CAPE,
	IT_2HANDED,
	IT_QUIVER,
	IT_TABARD,
	IT_ROBE,
	IT_RIGHTHANDED, // IT_CLAW
	IT_LEFTHANDED, // IT_1HANDED
	IT_OFFHAND, // HOLDABLE
	IT_AMMO, // unused?
	IT_THROWN,
	IT_GUN,
	IT_UNUSED,
	IT_RELIC,

	NUM_ITEM_TYPES
};

enum ModelLightTypes {
	MODELLIGHT_DIRECTIONAL=0,
	MODELLIGHT_POINT
};

// copied from the .mdl docs? this might be completely wrong
/*
Blending mode
Value	 Mapped to	 Meaning
0	 0	 Combiners_Opaque
1	 1	 Combiners_Mod
2	 1	 Combiners_Decal
3	 1	 Combiners_Add
4	 1	 Combiners_Mod2x
5	 4	 Combiners_Fade
6	 4	 Used in the Deeprun Tram subway glass, supposedly (src=dest_color, dest=src_color) (?)
*/
enum BlendModes {
	BM_OPAQUE,
	BM_TRANSPARENT,
	BM_ALPHA_BLEND,
	BM_ADDITIVE,
	BM_ADDITIVE_ALPHA,
	BM_MODULATE,
	BM_MODULATEX2
};

/*
Texture Types
Texture type is 0 for regular textures, nonzero for skinned textures (filename not referenced in the M2 file!) For instance, in the NightElfFemale model, her eye glow is a type 0 texture and has a file name, the other 3 textures have types of 1, 2 and 6. The texture filenames for these come from client database files:
DBFilesClient\CharSections.dbc
DBFilesClient\CreatureDisplayInfo.dbc
DBFilesClient\ItemDisplayInfo.dbc
(possibly more)
*/
enum TextureTypes {
	TEXTURE_FILENAME=0,			// Texture given in filename
	TEXTURE_BODY,				// Body + clothes
	TEXTURE_CAPE,				// Item, Capes ("Item\ObjectComponents\Cape\*.blp")
	TEXTURE_ITEM=TEXTURE_CAPE,
	TEXTURE_ARMORREFLECT,		//
	TEXTURE_HAIR=6,				// Hair, bear
	TEXTURE_FUR=8,				// Tauren fur
	TEXTURE_INVENTORY_ART1,		// Used on inventory art M2s (1): inventoryartgeometry.m2 and inventoryartgeometryold.m2
	TEXTURE_QUILL,				// Only used in quillboarpinata.m2. I can't even find something referencing that file. Oo Is it used?
	TEXTURE_GAMEOBJECT1,		// Skin for creatures or gameobjects #1
	TEXTURE_GAMEOBJECT2,		// Skin for creatures or gameobjects #2
	TEXTURE_GAMEOBJECT3,		// Skin for creatures or gameobjects #3
	TEXTURE_INVENTORY_ART2,		// Used on inventory art M2s (2): ui-buffon.m2 and forcedbackpackitem.m2 (LUA::Model:ReplaceIconTexture("texture"))
	TEXTURE_15,					// Patch 12857, Unknown
	TEXTURE_16,					//
	TEXTURE_17,					//
};

enum EyeGlowTypes {
	EGT_NONE = 0,
	EGT_DEFAULT,
	EGT_DEATHKNIGHT
};


#endif /* _WOW_ENUMS_H_ */
