#ifndef MODELHEADERS_H
#define MODELHEADERS_H

#pragma pack(push,1)

#include "vec3d.h"
#include "types.h" // unit8, etC.

struct Vertex {
    float tu, tv;
    float x, y, z;
};

struct Sphere
{
  /*0x00*/ Vec3D min;
  /*0x0C*/ Vec3D max;
  /*0x18*/ float radius;
};

struct CharModelDetails {
  bool closeRHand;
  bool closeLHand;

  bool isChar;

  void Reset() {
    closeRHand = false;
    closeLHand = false;
    isChar = false;
  }
};

/* global flags
uint32_t flag_tilt_x : 1;
uint32_t flag_tilt_y : 1;
uint32_t: 1;
uint32_t flag_use_texture_combiner_combos : 1;      // add textureCombinerCombos array to end of data
uint32_t: 1;
uint32_t flag_load_phys_data : 1;
uint32_t: 1;
uint32_t flag_unk_0x80 : 1;                         // with this flag unset, demon hunter tattoos stop glowing
// since Cata (4.0.1.12911) every model now has this flag
uint32_t flag_camera_related : 1;                   // TODO: verify version
uint32_t flag_new_particle_record : 1;              // In CATA: new version of ParticleEmitters. By default, length of M2ParticleOld is 476.
// But if 0x200 is set or if version is bigger than 271, length of M2ParticleOld is 492.
uint32_t flag_unk_0x400 : 1;
uint32_t flag_texture_transforms_use_bone_sequences : 1; // >= WoD 0x800 -- When set, texture transforms are animated using the sequence being played on the bone found by index in tex_unit_lookup_table[textureTransformIndex], instead of using the sequence being played on the model's first bone. Example model: 6DU_HellfireRaid_FelSiege03_Creature
uint32_t flag_unk_0x1000 : 1;
uint32_t flag_unk_0x2000 : 1;                       // seen in various legion models
uint32_t flag_unk_0x4000 : 1;
uint32_t flag_unk_0x8000 : 1;                       // seen in UI_MainMenu_Legion
uint32_t flag_unk_0x10000 : 1;
uint32_t flag_unk_0x20000 : 1;
uint32_t flag_unk_0x40000 : 1;
uint32_t flag_unk_0x80000 : 1;
uint32_t flag_unk_0x100000 : 1;
uint32_t flag_unk_0x200000 : 1;                     // apparently: use 24500 upgraded model format: chunked .anim files, change in the exporter reordering sequence+bone blocks before name
*/

struct ModelHeader {
  char id[4];                           // "MD20". Legion uses a chunked file format starting with MD21.
  uint8 version[4];
  uint32 nameLength;                    // should be globally unique, used to reload by name in internal clients
  uint32 nameOfs;
  uint32 globalFlags;                   // see above
  uint32 nGlobalLoops;                  // Timestamps used in global looping animations.
  uint32 ofsGlobalLoops;
  uint32 nSequences;                    // Information about the animations in the model.
  uint32 ofsSequences; 
  uint32 nAnimationLookup;              // Mapping of sequence IDs to the entries in the Animation sequences block.
  uint32 ofsAnimationLookup; 
  uint32 nBones;                        // MAX_BONES = 0x100 => Creature\SlimeGiant\GiantSlime.M2 has 312 bones (WOTLK)
  uint32 ofsBones;
  uint32 nKeyBoneLookup;                // Lookup table for key skeletal bones.
  uint32 ofsKeyBoneLookup; 
  uint32 nVertices; 
  uint32 ofsVertices; 
  uint32 nSkinProfiles;                 // Views (LOD) are now in .skins
  uint32 nColors;                       // Color and alpha animations definitions.
  uint32 ofsColors; 
  uint32 nTextures; 
  uint32 ofsTextures; 
  uint32 nTextureWeights;               // Transparency of textures.
  uint32 ofsTextureWeights;   
  uint32 nTextureTransforms;
  uint32 ofsTextureTransforms;
  uint32 nTextureReplaceLookup;
  uint32 ofsTextureReplaceLookup;
  uint32 nMaterials;                    // Blending modes / render flags.
  uint32 ofsMaterials; 
  uint32 nBoneLookup; 
  uint32 ofsBoneLookup; 
  uint32 nTextureLookup; 
  uint32 ofsTextureLookup; 
  uint32 nTextureUnitLookup;            // >= Cata : unused
  uint32 ofsTextureUnitLookup; 
  uint32 nTransparencyLookup; 
  uint32 ofsTransparencyLookup; 
  uint32 nTextureTransformLookup; 
  uint32 ofsTextureTransformLookup; 
  Sphere boundSphere;
  Sphere collisionSphere;
  uint32 nCollisionTriangles; 
  uint32 ofsCollisionTriangles;
  uint32 nCollisionVertices; 
  uint32 ofsCollisionVertices;
  uint32 nCollisionNormals; 
  uint32 ofsCollisionNormals;
  uint32 nAttachments;                  // position of equipped weapons or effects
  uint32 ofsAttachments;
  uint32 nAttachLookup; 
  uint32 ofsAttachLookup;
  uint32 nEvents;                       // Used for playing sounds when dying and a lot else.
  uint32 ofsEvents;
  uint32 nLights;                       // Lights are mainly used in loginscreens but in wands and some doodads too.
  uint32 ofsLights;
  uint32 nCameras;                      // The cameras are present in most models for having a model in the character tab.
  uint32 ofsCameras;
  uint32 nCameraLookup;
  uint32 ofsCameraLookup; 
  uint32 nRibbonEmitters;               // Things swirling around. See the CoT-entrance for light-trails.
  uint32 ofsRibbonEmitters;
  uint32 nParticleEmitters;             
  uint32 ofsParticleEmitters;
  uint32 nTextureCombinerCombos;        // When set, textures blending is overriden by the associated array.
  uint32 ofsTextureCombinerCombos;
};

#define ANIMATION_STORED_IN_M2 0x20 // primary bone sequence -- If set, the animation data is in the.m2 file.If not set, the animation data is in an.anim file.
#define ANIMATION_IS_ALIAS     0x40 // has next / is alias (To find the animation data, the client skips these by following aliasNext until an animation without 0x40 is found.)
#define ANIMATION_IS_BLENDED   0x80 // Blended animation (if either side of a transition has 0x80, lerp between end->start states, unless end==start by comparing bone values) 

// block B - animations, size 68 bytes, WotLK 64 bytes
struct ModelAnimation {
  int16 animID;         // Animation id in AnimationData.dbc
  int16 subAnimID;      // Sub-animation id: Which number in a row of animations this one is.
  uint32 duration;      // The length of this animation sequence in milliseconds.
  float moveSpeed;      // This is the speed the character moves with in this animation.
  uint32 flags;         // See above.
  int16 frequency;      // This is used to determine how often the animation is played. For all animations of the same type, this adds up to 0x7FFF (32767).
  uint16 unused;
  uint32 replay_min;    // May both be 0 to not repeat. Client will pick a random number of repetitions within bounds if given.
  uint32 replay_max;
  uint16 blendTimeIn;   // The client blends (lerp) animation states between animations where the end and start values differ. This specifies how long that blending takes. Values: 0, 50, 100, 150, 200, 250, 300, 350, 500.
  uint16 blendTimeOut;  // The client blends between this sequence and the next sequence for blendTimeOut milliseconds.
  // For both blendTimeIn and blendTimeOut, the client plays both sequences simultaneously while interpolating between their animation transforms.
  Sphere bounds;
  int16 NextAnimation;  // id of the following animation of this AnimationID, points to an Index or is -1 if none.
  int16 Index;          // id in the list of animations. Used to find actual animation if this sequence is an alias (flags & 0x40)
};

// sub-block in block E - animation data, size 28 bytes, WotLK 20 bytes
struct AnimationBlock {
  int16 type;    // interpolation type (0=none, 1=linear, 2=hermite)
  int16 seq;    // global sequence id or -1
  uint32 nTimes;
  uint32 ofsTimes;
  uint32 nKeys;
  uint32 ofsKeys;
};

struct FakeAnimationBlock {
  uint32 nTimes;
  uint32 ofsTimes;
  uint32 nKeys;
  uint32 ofsKeys;
};

struct AnimationBlockHeader
{
  uint32 nEntrys;
  uint32 ofsEntrys;
};

#define  MODELBONE_SPHERICAL_BILLBOARD  0x8
#define  MODELBONE_CYLINDRICAL_BILLBOARD_LOCK_X  0x10
#define  MODELBONE_CYLINDRICAL_BILLBOARD_LOCK_Y  0x20
#define  MODELBONE_CYLINDRICAL_BILLBOARD_LOCK_Z  0x40
#define  MODELBONE_TRANSFORMED  0x200
#define  MODELBONE_KINEMATIC  0x400  // MoP+: allow physics to influence this bone
#define  MODELBONE_HELMET_ANIM_SCALED 0x1000 // set blend_modificator to helmetAnimScalingRec.m_amount for this bone

// block E - bones
struct ModelBoneDef {
  int32 keyboneid; // Back-reference to the key bone lookup table. -1 if this is no key bone.
  int32 flags; // Only known flags: 8 - billboarded and 512 - transformed
  int16 parent; // parent bone index
  int16 geoid; // A geoset for this bone.
  int32 unknown; // new int added to the bone definitions.  Added in WoW 2.0
  AnimationBlock translation; // (Vec3D)
  AnimationBlock rotation; // (QuatS)
  AnimationBlock scaling; // (Vec3D)
  Vec3D pivot;
};

struct ModelTexAnimDef {
  AnimationBlock trans; // (Vec3D)
  AnimationBlock rot; // (QuatS)
  AnimationBlock scale; // (Vec3D)
};

struct ModelVertex {
  Vec3D pos;
  uint8 weights[4];
  uint8 bones[4];
  Vec3D normal;
  Vec2D texcoords[2];
};

/// Lod part,
struct M2SkinProfile
{
  char magic[4];         // Signature
  uint32 nVertices;
  uint32 ofsVertices; // int16, Vertices in this model (index into vertices[])
  uint32 nIndices;
  uint32 ofsIndices;   // int16[3], indices
  uint32 nBones;
  uint32 ofsBones;
  uint32 nSubmeshes;
  uint32 ofsSubmeshes;
  uint32 nBatches;
  uint32 ofsBatches;
  uint32 boneCountMax;
  uint32 nShadowBatches;
  uint32 ofsShadowBatches;
};


struct M2SkinSection
{
  uint16 skinSectionId;       // Mesh part ID, see below.
  uint16 level;               // (level << 16) is added (|ed) to startTriangle and alike to avoid having to increase those fields to uint32s.
  uint16 vertexStart;         // Starting vertex number.
  uint16 vertexCount;         // Number of vertices.
  uint16 indexStart;          // Starting triangle index (that's 3* the number of triangles drawn so far).
  uint16 indexCount;          // Number of triangle indices.
  uint16 boneCount;           // Number of elements in the bone lookup table. Max seems to be 256 in Wrath. Shall be different from 0.
  uint16 boneComboIndex;      // Starting index in the bone lookup table.
  uint16 boneInfluences;      // <= 4
  // from <=BC documentation: Highest number of bones needed at one time in this Submesh --Tinyn (wowdev.org) 
  // In 2.x this is the amount of of bones up the parent-chain affecting the submesh --NaK
  // Highest number of bones referenced by a vertex of this submesh. 3.3.5a and suspectedly all other client revisions. -- Skarn
  uint16 centerBoneIndex;
  Vec3D centerPosition;     // Average position of all the vertices in the sub mesh.
  Vec3D sortCenterPosition; // The center of the box when an axis aligned box is built around the vertices in the submesh.
  float sortRadius;             // Distance of the vertex farthest from CenterBoundingBox.
};

// same as ModelGeoset but with a uint32 as istart, to handle index > 65535 (present in HD models)
class M2SkinSectionHD
{
  public:
    M2SkinSectionHD():
      skinSectionId(-1), level(0), vertexStart(0), vertexCount(0),
      indexStart(0), indexCount(0), boneCount(0),
      boneComboIndex(0), boneInfluences(0), centerBoneIndex(0), sortRadius(0), display(false),
      centerPosition(Vec3D(0, 0, 0)), sortCenterPosition(Vec3D(0, 0, 0))
      { 
      }

    M2SkinSectionHD(M2SkinSection & geo):
      skinSectionId(geo.skinSectionId), vertexStart(geo.vertexStart), vertexCount(geo.vertexCount),
      indexStart(geo.indexStart), indexCount(geo.indexCount), boneCount(geo.boneCount),
      boneComboIndex(geo.boneComboIndex), boneInfluences(geo.boneInfluences), centerBoneIndex(geo.centerBoneIndex),
      sortRadius(geo.sortRadius), display(false), centerPosition(geo.centerPosition), sortCenterPosition(geo.sortCenterPosition)
      {
      }

    M2SkinSectionHD(const M2SkinSectionHD & geo):
      skinSectionId(geo.skinSectionId), level(geo.level), vertexStart(geo.vertexStart), vertexCount(geo.vertexCount),
      indexStart(geo.indexStart), indexCount(geo.indexCount), boneCount(geo.boneCount),
      boneComboIndex(geo.boneComboIndex), boneInfluences(geo.boneInfluences), centerBoneIndex(geo.centerBoneIndex),
      sortRadius(geo.sortRadius), display(geo.display), centerPosition(geo.centerPosition), sortCenterPosition(geo.sortCenterPosition)
      {
      }

    uint16 skinSectionId;       // Mesh part ID, see below.
    uint16 level;               // (level << 16) is added (|ed) to startTriangle and alike to avoid having to increase those fields to uint32s.
    uint16 vertexStart;         // Starting vertex number.
    uint16 vertexCount;         // Number of vertices.
    uint32 indexStart;          // Starting triangle index (that's 3* the number of triangles drawn so far).
    uint16 indexCount;          // Number of triangle indices.
    uint16 boneCount;           // Number of elements in the bone lookup table. Max seems to be 256 in Wrath. Shall be different from 0.
    uint16 boneComboIndex;      // Starting index in the bone lookup table.
    uint16 boneInfluences;      // <= 4
    // from <=BC documentation: Highest number of bones needed at one time in this Submesh --Tinyn (wowdev.org) 
    // In 2.x this is the amount of of bones up the parent-chain affecting the submesh --NaK
    // Highest number of bones referenced by a vertex of this submesh. 3.3.5a and suspectedly all other client revisions. -- Skarn
    uint16 centerBoneIndex;
    Vec3D centerPosition;     // Average position of all the vertices in the sub mesh.
    Vec3D sortCenterPosition; // The center of the box when an axis aligned box is built around the vertices in the submesh.
    float sortRadius;             // Distance of the vertex farthest from CenterBoundingBox.
    bool display;
};



#define  TEXTUREUNIT_STATIC  16
struct M2Batch
{
  uint8 flags;                       // Usually 16 for static textures, and 0 for animated textures. &0x1: materials invert something; &0x2: transform &0x4: projected texture; &0x10: something batch compatible; &0x20: projected texture?; &0x40: use textureWeights
  int8 priorityPlane;
  uint16 shader_id;                  // See below.
  uint16 skinSectionIndex;           // A duplicate entry of a submesh from the list above.
  uint16 geosetIndex;                // See below.
  uint16 colorIndex;                 // A Color out of the Colors-Block or -1 if none.
  uint16 materialIndex;              // The renderflags used on this texture-unit.
  uint16 materialLayer;              // Capped at 7 (see CM2Scene::BeginDraw)
  uint16 textureCount;               // 1 to 4. See below. Also seems to be the number of textures to load, starting at the texture lookup in the next field (0x10).
  uint16 textureComboIndex;          // Index into Texture lookup table
  uint16 textureCoordComboIndex;     // Index into the texture unit lookup table.
  uint16 textureWeightComboIndex;    // Index into transparency lookup table.
  uint16 textureTransformComboIndex; // Index into uvanimation lookup table. 
};

/*
Shader thingey
Its actually two uint8s defining the shader used. Everything below this is in binary. X represents a variable digit.
Depending on "Mode", its either "Diffuse_%s_%s" and "Combiners_%s_%s" (Mode=0) or "Diffuse_%s" and "Combiners_%s" (Mode>0).


Diffuse
Mode   Shading     String
0      0XXX 0XXX   Diffuse_T1_T2
0      0XXX 1XXX   Diffuse_T1_Env
0      1XXX 0XXX   Diffuse_Env_T2
0      1XXX 1XXX   Diffuse_Env_Env

1      0XXX XXXX   Diffuse_T1
1      1XXX XXXX   Diffuse_Env

Combiners
Mode   Shading     String
0      X000 XXXX   Combiners_Opaque_%s
0      X001 XXXX   Combiners_Mod_%s
0      X011 XXXX   Combiners_Add_%s
0      X100 XXXX   Combiners_Mod2x_%s

0      XXXX X000   Combiners_%s_Opaque
0      XXXX X001   Combiners_%s_Mod
0      XXXX X011   Combiners_%s_Add
0      XXXX X100   Combiners_%s_Mod2x
0      XXXX X110   Combiners_%s_Mod2xNA
0      XXXX X111   Combiners_%s_AddNA

1      X000 XXXX   Combiners_Opaque
1      X001 XXXX   Combiners_Mod
1      X010 XXXX   Combiners_Decal
1      X011 XXXX   Combiners_Add
1      X100 XXXX   Combiners_Mod2x
1      X101 XXXX   Combiners_Fade

*/

// block X - render flags
/* flags */
#define  RENDERFLAGS_UNLIT      0x01 // Unlit
#define  RENDERFLAGS_UNFOGGED   0x02 // Unfogged
#define  RENDERFLAGS_TWOSIDED   0x04 // Two - sided(no backface culling if set)
#define  RENDERFLAGS_BILLBOARD  0x08 // depthTest
#define  RENDERFLAGS_ZBUFFERED  0x10 // depthWrite
#define  RENDERFLAGS_UNK1       0x40 // shadow batch related ? ? ? (seen in WoD)
#define  RENDERFLAGS_UNK2       0x80 // shadow batch related ? ? ? (seen in WoD)
#define  RENDERFLAGS_UNK3       0x400 // ? ? ? (seen in WoD)
#define  RENDERFLAGS_NOALPHA    0x800 // prevent alpha for custom elements. if set, use(fully) opaque or transparent. (litSphere, shadowMonk) (MoP + )

struct M2Material {
  uint16 flags;
  uint16 blend; // see enums.h, enum BlendModes
};

// block G - color defs
// For some swirling portals and volumetric lights, these define vertex colors. 
// Referenced from the Texture Unit blocks in the LOD part. Contains a separate timeline for transparency values. 
// If no animation is used, the given value is constant.
struct ModelColorDef {
  AnimationBlock color; // (Vec3D) Three floats. One for each color.
  AnimationBlock opacity; // (UInt16) 0 - transparent, 0x7FFF - opaque.
};

// block H - transparency defs
struct M2TextureWeight {
  AnimationBlock weight; // (UInt16)
};

struct ModelTextureDef {
  uint32 type;
  uint32 flags;
  uint32 nameLen;
  uint32 nameOfs;
};

struct ModelLightDef {
  int16 type; // 0: Directional, 1: Point light
  int16 bone; // If its attached to a bone, this is the bone. Else here is a nice -1.
  Vec3D pos; // Position, Where is this light?
  AnimationBlock ambientColor; // (Vec3D) The ambient color. Three floats for RGB.
  AnimationBlock ambientIntensity; // (Float) A float for the intensity.
  AnimationBlock diffuseColor; // (Vec3D) The diffuse color. Three floats for RGB.
  AnimationBlock diffuseIntensity; // (Float) A float for the intensity again.
  AnimationBlock attenuationStart; // (Float) This defines, where the light starts to be.
  AnimationBlock attenuationEnd; // (Float) And where it stops.
  AnimationBlock useAttenuation; // (Uint32) Its an integer and usually 1.
};

struct ModelCameraDef {
  int32 id; // 0 is potrait camera, 1 characterinfo camera; -1 if none; referenced in CamLookup_Table
  float fov; // No radians, no degrees. Multiply by 35 to get degrees.
  float farclip; // Where it stops to be drawn.
  float nearclip; // Far and near. Both of them.
  AnimationBlock transPos; // (Vec3D) How the cameras position moves. Should be 3*3 floats. (? WoW parses 36 bytes = 3*3*sizeof(float))
  Vec3D pos; // float, Where the camera is located.
  AnimationBlock transTarget; // (Vec3D) How the target moves. Should be 3*3 floats. (?)
  Vec3D target; // float, Where the camera points to.
  AnimationBlock rot; // (Quat) The camera can have some roll-effect. Its 0 to 2*Pi.
};

struct ModelCameraDefV10 {
  int32 id; // 0 is potrait camera, 1 characterinfo camera; -1 if none; referenced in CamLookup_Table
  float farclip; // Where it stops to be drawn.
  float nearclip; // Far and near. Both of them.
  AnimationBlock transPos; // (Vec3D) How the cameras position moves. Should be 3*3 floats. (? WoW parses 36 bytes = 3*3*sizeof(float))
  Vec3D pos; // float, Where the camera is located.
  AnimationBlock transTarget; // (Vec3D) How the target moves. Should be 3*3 floats. (?)
  Vec3D target; // float, Where the camera points to.
  AnimationBlock rot; // (Quat) The camera can have some roll-effect. Its 0 to 2*Pi. 3 Floats!
  AnimationBlock AnimBlock4; // (Float) One Float. cataclysm
};

struct ModelParticleParams {
  FakeAnimationBlock colors;   // (Vec3D)  This one points to 3 floats defining red, green and blue.
  FakeAnimationBlock opacity;      // (UInt16)    Looks like opacity (short), Most likely they all have 3 timestamps for {start, middle, end}.
  FakeAnimationBlock sizes;     // (Vec2D)  It carries two floats per key. (x and y scale)
  int32 d[2];
  FakeAnimationBlock Intensity;   // (UInt16) Some kind of intensity values seen: 0,16,17,32(if set to different it will have high intensity)
  FakeAnimationBlock unk2;     // (UInt16)
  float unk[3];
  Vec3D scales;
  float slowdown;
  float unknown1[2];
  float rotation;        //Sprite Rotation
  float unknown2[2];
  Vec3D Rot1;          //Model Rotation 1
  Vec3D Rot2;          //Model Rotation 2
  Vec3D Trans;        //Model Translation
  float f2[4];
  int32 nUnknownReference;
  int32 ofsUnknownReferenc;
};

// Most of these model particle flags are currently ignored by WMV:
#define MODELPARTICLE_FLAGS_WORLDSPACE     0x8        // Particles travel "up" in world space, rather than model
#define MODELPARTICLE_FLAGS_DONOTTRAIL     0x10       // Do not trail 
#define MODELPARTICLE_FLAGS_MODELSPACE     0x80       // Particles in model space
#define MODELPARTICLE_FLAGS_PINNED         0x400      // Pinned Particles, their quad enlarges from their creation
                                                      // position to where they expand
#define MODELPARTICLE_FLAGS_DONOTBILLBOARD 0x1000     // Wiki says: XYQuad Particles. They align to XY axis
                                                      // facing Z axis direction
#define MODELPARTICLE_FLAGS_RANDOMTEXTURE  0x10000    // Choose Random Texture
#define MODELPARTICLE_FLAGS_OUTWARD        0x20000    // "Outward" particles, most emitters have this and
                                                      // their particles move away from the origin, when they
                                                      // don't the particles start at origin+(speed*life) and
                                                      // move towards the origin
#define MODELPARTICLE_FLAGS_RANDOMSTART    0x200000   // Random Flip Book Start
#define MODELPARTICLE_FLAGS_BONEGENERATOR  0x1000000  // Bone generator = bone, not joint
#define MODELPARTICLE_FLAGS_DONOTTHROTTLE  0x4000000  // Do not throttle emission rate based on distance
#define MODELPARTICLE_FLAGS_MULTITEXTURE   0x10000000 // Particle uses multi-texturing. This affects emitter values
#define MODELPARTICLE_EMITTER_PLANE  1
#define MODELPARTICLE_EMITTER_SPHERE 2
#define MODELPARTICLE_EMITTER_SPLINE 3

template<typename Base, size_t integer_bits, size_t decimal_bits> struct fixed_point
{
  Base decimal : decimal_bits;
  Base integer : integer_bits;
  Base sign : 1;
  float to_float() const { return (sign ? -1.0f : 1.0f) * (integer + decimal / float (1 << decimal_bits)); }
};
using fp_6_9 = fixed_point<uint16, 6, 9>;
using fp_2_5 = fixed_point<uint8, 2, 5>;
struct vector_2fp_6_9 { fp_6_9 x; fp_6_9 y; };

struct M2ParticleDef
{
  int32 id;  // so far it's always -1
  int32 flags; // MODELPARTICLE_FLAGS_*
  Vec3D pos; // The position. Relative to the following bone.
  int16 bone; // The bone it's attached to.
  int16 texture; // And the texture that is used. In multitextured particles this is actually composed of
                  // three 5-bit texture ints, plus 1 bit left over.
  int32 nModelFileName;
  int32 ofsModelFileName;
  int32 nParticleFileName;
  int32 ofsParticleFileName; // TODO
  uint8 blend;
  uint8 EmitterType;    // EmitterType 1 - Plane (rectangle), 2 - Sphere, 3 - Spline? (can't be bothered to find one)
  uint16 ParticleColorIndex; // If used then it's usually 11, 12 or 13. This one is used so you can assign a color to specific particles. They loop over all
                        // particles and compare +0x2A to 11, 12 and 13. If that matches, the colors from the dbc get applied.
  fp_2_5 multiTextureParamX[2]; // used for multitextured particles, but not sure how, yet
  int16 TextureTileRotation; // TODO, Rotation for the texture tile. (Values: -1,0,1)
  uint16 rows; // How many different frames are on that texture? People should learn what rows and cols are.
  uint16 cols; // (2, 2) means slice texture to 2*2 pieces
  AnimationBlock EmissionSpeed; // (Float) All of the following blocks should be floats.
  AnimationBlock SpeedVariation; // (Float) Variation in the flying-speed. (range: 0 to 1)
  AnimationBlock VerticalRange; // (Float) Drifting away vertically. (range: 0 to pi)
  AnimationBlock HorizontalRange; // (Float) They can do it horizontally too! (range: 0 to 2*pi)
  AnimationBlock Gravity; // (Float)
  AnimationBlock Lifespan; // (Float)
  int32 unknown;
  AnimationBlock EmissionRate; // (Float) Spread your particles, emitter.
  int32 unknown2;
  AnimationBlock EmissionAreaLength; // (Float) Well, you can do that in this area.
  AnimationBlock EmissionAreaWidth; // (Float)
  AnimationBlock zSource; // When greater than 0, the initial velocity of the particle is (particle.position - C3Vector(0, 0, zSource)).Normalize()
  ModelParticleParams p;
  AnimationBlock EnabledIn; // (UInt16)
  vector_2fp_6_9 multiTextureParam0[2];
  vector_2fp_6_9 multiTextureParam1[2];
};

struct ModelRibbonEmitterDef {
  int32 id;
  int32 bone;
  Vec3D pos;
  int32 nTextures;
  int32 ofsTextures;
  int32 nUnknown;
  int32 ofsUnknown;
  AnimationBlock color; // (Vec3D)
  AnimationBlock opacity; // (UInt16) And an alpha value in a short, where: 0 - transparent, 0x7FFF - opaque.
  AnimationBlock above; // (Float) The height above.
  AnimationBlock below; // (Float) The height below. Do not set these to the same!
  float res; // This defines how smooth the ribbon is. A low value may produce a lot of edges.
  float length; // The length aka Lifespan.
  float Emissionangle; // use arcsin(val) to get the angle in degree
  int16 s1, s2;
  AnimationBlock unk1; // (short)
  AnimationBlock unk2; // (boolean)
  int32 unknown; // This looks much like just some Padding to the fill up the 0x10 Bytes, always 0
};

/* 
These events are used for timing sounds for example. You can find the $DTH (death) event on nearly every model. It will play the death sound for the unit.
The events you can use depend on the way, the model is used. Dynamic objects can shake the camera, doodads shouldn't. Units can do a lot more than other objects.
Somehow there are some entries, that don't use the $... names but identifiers like "DEST" (destination), "POIN" (point) or "WHEE" (wheel). How they are used? Idk.
*/
struct ModelEventDef {
  char id[4]; // This is a (actually 3 character) name for the event with a $ in front.
  int32 dbid; // This data is passed when the event is fired.
  int32 bone; // Somewhere it has to be attached.
  Vec3D pos; // Relative to that bone of course.
  int16 type; // This is some fake-AnimationBlock.
  int16 seq; // Built up like a real one but without timestamps(?). What the fuck?
  uint32 nTimes; // See the documentation on AnimationBlocks at this topic.
  uint32 ofsTimes; // This points to a list of timestamps for each animation given.
};
/*
There are a lot more of them. I did not list all up to now.
ID   Data   Description
DEST     exploding ballista, that one has a really fucked up block. Oo
POIN   unk   something alliance gunship related (flying in icecrown)
WHEE   601+   Used on wheels at vehicles.
$tsp     p is {0 to 3} (position); t is {W, S, B, F (feet) or R} (type); s is {R or L} (right or left); this is used when running through snow for example.
$AHx     UnitCombat_C, x is {0 to 3}
$BRT     Plays some sound.
$BTH     Used for bubbles or breath. ("In front of head")
$BWP     UnitCombat_C
$BWR     Something with bow and rifle. Used in AttackRifle, AttackBow etc. "shoot now"?
$CAH     UnitCombat_C
$Cxx     UnitCombat_C, x is {P or S}
$CSD  SoundEntries.dbc   Emote sounds?
$CVS  SoundEntriesAdvanced.dbc   Sound
$DSE    
$DSL  SoundEntries.dbc   Sound with something special. Use another one if you always want to have it playing..
$DSO  SoundEntries.dbc   Sound
$DTH     UnitCombat_C, death, this plays death sounds and more.
$EMV     MapLoad.cpp
$ESD     Plays some emote sound.
$EWT     MapLoad.cpp
$FDx     x is {1 to 5}. Calls some function in the Object VMT. Also plays some sound.
$FDx     x is {6 to 9}. Calls some function in the Object VMT.
$FDX     Should do nothing. But is existant.
$FSD     Plays some sound.
$GCx     Play gameobject custom sound referenced in GameObjectDisplayInfo.dbc. x can be from {0 to 3}: {Custom0, Custom1, Custom2, Custom3}
$GOx     Play gameobject sound referenced in GameObjectDisplayInfo.dbc. x can be from {0 to 5}: {Stand, Open, Loop, Close, Destroy, Opened}
$HIT     Get hit?
$KVS     MapLoad.cpp
$SCD     Plays some sound.
$SHK  SpellEffectCameraShakes.dbc   Add a camera shake
$SHx     x is {L or R}, fired on Sheath and SheathHip. "Left/right shoulder" was in the old list.
$SMD     Plays some sound.
$SMG     Plays some sound.
$SND  SoundEntries.dbc   Sound
$TRD     Does something with a spell, a sound and a spellvisual.
$VGx     UnitVehicle_C, x is {0 to 8}
$VTx     UnitVehicle_C, x is {0 to 8}
$WxG     x is {W or N}. Calls some function in the Object VMT.
-------   ----------------------------------   - Old documentation (?) ----------------------------------------------
$CSx     x is {L or R} ("Left/right hand") (?)
$CFM    
$CHD     ("Head") (?)
$CCH     ("Bust") (?)
$TRD     ("Crotch") (?)
$CCH     ("Bust") (?)
$BWR     ("Right hand") (?)
$CAH    
$CST
*/



/*
 * This block specifies a bunch of locations on the body - hands, shoulders, head, back, 
 * knees etc. It is used to put items on a character. This seems very likely as this block 
 * also contains positions for sheathed weapons, a shield, etc.
 */
struct ModelAttachmentDef {
  uint32 id; // Just an id. Is referenced in the enum POSITION_SLOTS.
  uint32 bone; // Somewhere it has to be attached.
  Vec3D pos; // Relative to that bone of course.
  AnimationBlock unk; // (Int32) Its an integer in the data. It has been 1 on all models I saw. Whatever.
};

#pragma pack(pop)
#endif
