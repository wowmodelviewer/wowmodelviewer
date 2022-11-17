/*
 * wow_enums.h
 *
 *  Created on: 7 Aug. 2015
 *      Author: Jeromnimo
 */

#ifndef _WOW_ENUMS_H_
#define _WOW_ENUMS_H_

enum CharSlots
{
  CS_HEAD,
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

enum CharGeosets
{
  CG_SKIN_OR_HAIR             = 0,
  CG_FACE_1                   = 1,
  CG_FACE_2                   = 2,
  CG_FACE_3                   = 3,
  CG_GLOVES                   = 4,
  CG_BOOTS                    = 5,
  CG_TAIL                     = 6,
  CG_EARS                     = 7,
  CG_SLEEVES                  = 8,
  CG_KNEEPADS                 = 9,
  CG_CHEST                    = 10,
  CG_PANTS                    = 11,
  CG_TABARD                   = 12,
  CG_TROUSERS	              = 13,
  CG_DH_LOINCLOTH             = 14,
  CG_CLOAK                    = 15,
  CG_EYEGLOW                  = 17,
  CG_BELT                     = 18,
  CG_BONE                     = 19,
  CG_FEET                     = 20,
  CG_GEOSET2100               = 21,
  CG_TORSO                    = 22,
  CG_HAND_ATTACHMENT          = 23,
  CG_HEAD_ATTACHMENT          = 24,
  CG_DH_BLINDFOLDS            = 25,
  CG_GEOSET2600               = 26,
  CG_GEOSET2700               = 27,
  CG_GEOSET2800               = 28,
  CG_MECHAGNOME_ARMS_OR_HANDS = 29,
  CG_MECHAGNOME_LEGS          = 30,
  CG_MECHAGNOME_FEET          = 31,
  CG_FACE                     = 32,
  CG_EYES                     = 33,
  CG_EYEBROWS                 = 34,
  CG_EARRINGS                 = 35,
  CG_NECKLACE                 = 36,
  CG_HEADDRESS                = 37,
  CG_TAILS                    = 38,
  CG_VINES                    = 39,
  CG_TUSKS                    = 40,
  CG_NOSES                    = 41,
  CG_HAIR_DECORATION          = 42,
  CG_HORN_DECORATION          = 43,
  NUM_GEOSETS
};

#define  UPPER_BODY_BONES  5

enum POSITION_SLOTS
{ // wxString Attach_Names[]
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
  ATT_SHEATH_CROSSBOW,
  ATT_HEAD_TOP
};

enum CharRegions
{
  // Corresponds to "Section" column in CharComponentTextureSection table
  CR_ARM_UPPER = 0,
  CR_ARM_LOWER = 1,
  CR_HAND = 2,
  CR_TORSO_UPPER = 3,
  CR_TORSO_LOWER = 4,
  CR_LEG_UPPER = 5,
  CR_LEG_LOWER = 6,
  CR_FOOT = 7,
  CR_UNK8 = 8,
  CR_FACE_UPPER = 9,
  CR_FACE_LOWER = 10,
  CR_DH_TATTOOS = 12,
  CR_CAPE = 13,
  // Not compositing anymore, only used for items
  CR_TABARD_1,
  CR_TABARD_2,
  CR_TABARD_3,
  CR_TABARD_4,
  CR_TABARD_5,
  CR_TABARD_6
};

enum KeyBoneTable
{ // wxString Bone_Names[]
  //Block F - Key Bone lookup table.
  //---------------------------------
  BONE_LARM = 0,    // 0, ArmL: Left upper arm
  BONE_RARM,      // 1, ArmR: Right upper arm
  BONE_LSHOULDER,    // 2, ShoulderL: Left Shoulder / deltoid area
  BONE_RSHOULDER,    // 3, ShoulderR: Right Shoulder / deltoid area
  BONE_STOMACH,    // 4, SpineLow: (upper?) abdomen
  BONE_WAIST,      // 5, Waist: (lower abdomen?) waist
  BONE_HEAD,      // 6, Head
  BONE_JAW,      // 7, Jaw: jaw/mouth
  BONE_RFINGER1,    // 8, IndexFingerR: (Trolls have 3 "fingers", this points to the 2nd one.
  BONE_RFINGER2,    // 9, MiddleFingerR: center finger - only used by dwarfs.. don't know why
  BONE_RFINGER3,    // 10, PinkyFingerR: (Trolls have 3 "fingers", this points to the 3rd one.
  BONE_RFINGERS,    // 11, RingFingerR: Right fingers -- this is -1 for trolls, they have no fingers, only the 3 thumb like thingys
  BONE_RTHUMB,    // 12, ThumbR: Right Thumb
  BONE_LFINGER1,    // 13, IndexFingerL: (Trolls have 3 "fingers", this points to the 2nd one.
  BONE_LFINGER2,    // 14, MiddleFingerL: Center finger - only used by dwarfs.
  BONE_LFINGER3,    // 15, PinkyFingerL: (Trolls have 3 "fingers", this points to the 3rd one.
  BONE_LFINGERS,    // 16, RingFingerL: Left fingers
  BONE_LTHUMB,    // 17, ThubbL: Left Thumb
  BONE_BTH,      // 18, $BTH: In front of head
  BONE_CSR,      // 19, $CSR: Left hand
  BONE_CSL,      // 20, $CSL: Left hand
  BONE_BREATH,    // 21, _Breath
  BONE_NAME,      // 22, _Name
  BONE_NAMEMOUNT,    // 23, _NameMount
  BONE_CHD,      // 24, $CHD: Head
  BONE_CCH,      // 25, $CCH: Bust
  BONE_ROOT,      // 26, Root: The "Root" bone,  this controls rotations, transformations, etc of the whole model and all subsequent bones.
  BONE_WHEEL1,    // 27, Wheel1
  BONE_WHEEL2,    // 28, Wheel2
  BONE_WHEEL3,    // 29, Wheel3
  BONE_WHEEL4,    // 30, Wheel4
  BONE_WHEEL5,    // 31, Wheel5
  BONE_WHEEL6,    // 32, Wheel6
  BONE_WHEEL7,    // 33, Wheel7
  BONE_WHEEL8,    // 34, Wheel8
  BONE_MAX
};

enum ModelType
{
  MT_NORMAL,
  MT_CHAR,
  MT_WMO,
  MT_NPC
};

enum SheathTypes
{
  SHEATHETYPE_NONE = 0,
  SHEATHETYPE_MAINHAND = 1,
  SHEATHETYPE_LARGEWEAPON = 2,
  SHEATHETYPE_HIPWEAPON = 3,
  SHEATHETYPE_SHIELD = 4
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

enum ModelLightTypes
{
  MODELLIGHT_DIRECTIONAL = 0,
  MODELLIGHT_POINT
};

// copied from the .mdl docs? this might be completely wrong
/*
Blending mode
Value   Mapped to   Meaning
0   0   Combiners_Opaque
1   1   Combiners_Mod
2   1   Combiners_Decal
3   1   Combiners_Add
4   1   Combiners_Mod2x
5   4   Combiners_Fade
6   4   Used in the Deeprun Tram subway glass, supposedly (src=dest_color, dest=src_color) (?)
*/
enum BlendModes
{
  BM_OPAQUE,
  BM_TRANSPARENT,
  BM_ALPHA_BLEND,
  BM_ADDITIVE,
  BM_ADDITIVE_ALPHA,
  BM_MODULATE,
  BM_MODULATEX2,
  BM_7,
};

/*
Texture Types
Texture type is 0 for regular textures, nonzero for skinned textures (filename not referenced in the M2 file!) For instance, 
in the NightElfFemale model, her eye glow is a type 0 texture and has a file name, 
the other 3 textures have types of 1, 2 and 6.
The texture filenames for these come from client database files:
DBFilesClient\CharSections.dbc
DBFilesClient\CreatureDisplayInfo.dbc
DBFilesClient\ItemDisplayInfo.dbc
(possibly more)
*/
enum TextureTypes
{
  TEXTURE_FILENAME = 0,         // Texture given in filename
  TEXTURE_SKIN = 1,             // Body + clothes
  TEXTURE_OBJECT_SKIN = 2,      // Item, Capes ("Item\ObjectComponents\Cape\*.blp")
  TEXTURE_WEAPON_BLADE = 3,     //
  TEXTURE_WEAPON_HANDLE = 4,    //
  TEXTURE_ENVIRONMENT = 5,      // OBSOLETE
  TEXTURE_CHAR_HAIR = 6,        // Hair, bear
  TEXTURE_CHAR_FACIAL_HAIR = 7, // OBSOLETE
  TEXTURE_SKIN_EXTRA = 8,       // Used for component models for character customization (which we merge)
  TEXTURE_UI_SKIN = 9,          // Used on inventory art M2s (1): inventoryartgeometry.m2 and inventoryartgeometryold.m2
  TEXTURE_TAUREN_MANE = 10,     // OBSOLETE
  TEXTURE_GAMEOBJECT1 = 11,     // Skin for creatures or gameobjects #1
  TEXTURE_GAMEOBJECT2 = 12,     // Skin for creatures or gameobjects #2
  TEXTURE_GAMEOBJECT3 = 13,     // Skin for creatures or gameobjects #3
  TEXTURE_ITEM_ICON = 14,       // Used on inventory art M2s (2): ui-buffon.m2 and forcedbackpackitem.m2 (LUA::Model:ReplaceIconTexture("texture"))
  TEXTURE_15 = 15,              // Guild Background Color
  TEXTURE_16 = 16,              // Guild Emblem Color
  TEXTURE_17 = 17,              // Guild Border Color 
  TEXTURE_18 = 18               // Guild Emblem 
};

enum EyeGlowTypes
{
  EGT_NONE = 0,
  EGT_DEFAULT,
  EGT_DEATHKNIGHT
};

enum Gender
{
  GENDER_MALE = 0,
  GENDER_FEMALE = 1,
  GENDER_NONE = 2,
  GENDER_ANY = 3     // used in some database tables
};

// Race value is index in ChrRaces.dbc
enum Races
{
  RACE_ANY = 0,  // used in some database tables
  RACE_HUMAN = 1,
  RACE_ORC = 2,
  RACE_DWARF = 3,
  RACE_NIGHTELF = 4,
  RACE_UNDEAD = 5,
  RACE_TAUREN = 6,
  RACE_GNOME = 7,
  RACE_TROLL = 8,
  RACE_GOBLIN = 9,
  RACE_BLOODELF = 10,
  RACE_DRAENEI = 11,
  RACE_FEL_ORC = 12,
  RACE_NAGA = 13,
  RACE_BROKEN = 14,
  RACE_SKELETON = 15,
  RACE_VRYKUL = 16,
  RACE_TUSKARR = 17,
  RACE_FOREST_TROLL = 18,
  RACE_TAUNKA = 19,
  RACE_NORTHREND_SKELETON = 20,
  RACE_ICE_TROLL = 21,
  RACE_WORGEN = 22,
  RACE_GILNEAN = 23,
  RACE_PANDAREN = 24,    // Pandaren #1
  RACE_PANDAREN2 = 25,  // Pandaren #2
  RACE_PANDAREN3 = 26,  // Pandaren #3
  RACE_NIGHTBORNE = 27,
  RACE_HIGHMOUNTAIN_TAUREN = 28,
  RACE_VOID_ELF = 29,
  RACE_LIGHTFORGED_DRAENEI = 30,
  RACE_ZANDALARI_TROLL = 31,
  RACE_KUL_TIRAN = 32,
  RACE_THIN_HUMAN = 33,
  RACE_DARK_IRON_DWARF = 34,
  RACE_VULPERA = 35,
  RACE_MAGHAR_ORC = 36,
  RACE_MECHAGNOME = 37,
  RACE_DRACTHYR = 52,
  RACE_DRACTHYR2 = 70,
  RACE_COMPANIONDRAKE = 71,
  RACE_COMPANIONPROTODRAGON = 72,
  RACE_COMPANIONSERPENT = 73,
  RACE_COMPANIONWYVERN = 74,
  RACE_DRACTHYR3 = 75, // UnalteredVisualRaceID for RACE_DRACTHYR (Alliance)
  RACE_DRACTHYR4 = 76, // UnalteredVisualRaceID for RACE_DRACTHYR2 (Horde)
  RACE_COMPANIONPTERRODAX = 77
};

// Class value is index in ChrClasses.db*
enum Classes
{
  CLASS_ANY = 0,
  CLASS_WARRIOR = 1,
  CLASS_PALADIN = 2,
  CLASS_HUNTER = 3,
  CLASS_ROGUE = 4,
  CLASS_PRIEST = 5,
  CLASS_DEATHKNIGHT = 6,
  CLASS_SHAMAN = 7,
  CLASS_MAGE = 8,
  CLASS_WARLOCK = 9,
  CLASS_MONK = 10,
  CLASS_DRUID = 11,
  CLASS_DEMONHUNTER = 12,
  CLASS_EVOKER = 13,
  CLASS_ADVENTURER = 14 // Not sure what this is, added in Dragonflight
};

#endif /* _WOW_ENUMS_H_ */
