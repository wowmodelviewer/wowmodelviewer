#ifndef MODELHEADERS_H
#define MODELHEADERS_H

#pragma pack(push,1)

#include "glm/glm.hpp"

/*
struct Vertex {
  float tu, tv;
  float x, y, z;
};
*/

struct Sphere
{
  /*0x00*/  glm::vec3 min;
  /*0x0C*/  glm::vec3 max;
  /*0x18*/ float radius;
};

struct CAaBox
{
  glm::vec3 min;
  glm::vec3 max;
};

struct M2Bounds
{
  CAaBox extent;
  float radius;
};

struct M2Range
{
  uint32_t minimum;
  uint32_t maximum;
};

// In WoW 2.0+ Blizzard are now storing rotation data in 16bit values instead of 32bit.
// I don't really understand why as its only a very minor saving in model sizes and adds extra overhead in
// processing the models.  Need this structure to read the data into.
//struct PACK_QUATERNION {  
//  int16 x,y,z,w;  
//}; 
struct M2CompQuat
{
  __int16 x, y, z, w;
};

struct M2Material
{
  uint16_t flags;
  uint16_t blending_mode; // apparently a bitfield
};

template<typename T>
struct M2SplineKey
{
  T value;
  T inTan;
  T outTan;
};

struct CharModelDetails
{
  bool closeRHand;
  bool closeLHand;

  bool isChar;

  void Reset() {
    closeRHand = false;
    closeLHand = false;
    isChar = false;
  }
};

template<typename Base, size_t integer_bits, size_t decimal_bits> struct fixed_point
{
  Base decimal : decimal_bits;
  Base integer : integer_bits;
  Base sign : 1;
  float to_float() const { return (sign ? -1.0f : 1.0f) * (integer + decimal / float(1 << decimal_bits)); }
};
using fp_6_9 = fixed_point<uint16_t, 6, 9>;
using fp_2_5 = fixed_point<uint8_t, 2, 5>;
using fixed16 = fixed_point<uint16_t, 0, 15>;
struct vector_2fp_6_9 { fp_6_9 x; fp_6_9 y; };

struct M2Box
{
  glm::vec3 ModelRotationSpeedMin;
  glm::vec3 ModelRotationSpeedMax;
};

struct CRange
{
  float min;
  float max;
};

template<typename T>
struct M2Array
{
  uint32_t number;
  uint32_t offset;
};

struct M2Loop
{
  uint32_t timestamp;
};



#define  ANIMATION_HANDSCLOSED  15
#define  ANIMATION_MOUNT      91
#define  ANIMATION_LOOPED    0x20 // flags
// block B - animations, size 68 bytes, WotLK 64 bytes
struct M2Sequence
{
  uint16_t id;                   // Animation id in AnimationData.dbc
  uint16_t variationIndex;       // Sub-animation id: Which number in a row of animations this one is.
  uint32_t duration;             // The length of this animation sequence in milliseconds.
  float movespeed;               // This is the speed the character moves with in this animation.
  uint32_t flags;                // See below.
  int16_t frequency;             // This is used to determine how often the animation is played. For all animations of the same type, this adds up to 0x7FFF (32767).
  uint16_t _padding;
  M2Range replay;                // May both be 0 to not repeat. Client will pick a random number of repetitions within bounds if given.
  uint16_t blendTimeIn;          // The client blends (lerp) animation states between animations where the end and start values differ. This specifies how long that blending takes. Values: 0, 50, 100, 150, 200, 250, 300, 350, 500.
  uint16_t blendTimeOut;         // The client blends between this sequence and the next sequence for blendTimeOut milliseconds.
                                 // For both blendTimeIn and blendTimeOut, the client plays both sequences simultaneously while interpolating between their animation transforms.
  M2Bounds bounds;
  int16_t variationNext;         // id of the following animation of this AnimationID, points to an Index or is -1 if none.
  uint16_t aliasNext;            // id in the list of animations. Used to find actual animation if this sequence is an alias (flags & 0x40)
};

struct M2TrackBase
{
  uint16_t interpolation_type;
  uint16_t global_sequence;
  M2Array<M2Array<uint32_t>> timestamps;
};

template<typename T>
struct M2Track : M2TrackBase
{
  M2Array<M2Array<T>> values;
};

template<typename T>
struct FBlock
{
  M2Array<M2Array<uint32_t>> timestamps;
  M2Array<M2Array<T>> values;
};

struct AnimationBlockHeader
{
  uint32_t nEntrys;
  uint32_t ofsEntrys;
};

#define  MODELBONE_BILLBOARD  8
#define  MODELBONE_TRANSFORM  512
// block E - bones
struct M2CompBone
{
  int32_t key_bone_id;            // Back-reference to the key bone lookup table. -1 if this is no key bone.
  enum
  {
    ignoreParentTranslate = 0x1,
    ignoreParentScale = 0x2,
    ignoreParentRotation = 0x4,
    spherical_billboard = 0x8,
    cylindrical_billboard_lock_x = 0x10,
    cylindrical_billboard_lock_y = 0x20,
    cylindrical_billboard_lock_z = 0x40,
    transformed = 0x200,
    kinematic_bone = 0x400,       // MoP+: allow physics to influence this bone
    helmet_anim_scaled = 0x1000,  // set blend_modificator to helmetAnimScalingRec.m_amount for this bone
    something_sequence_id = 0x2000, // <=bfa+, parent_bone+submesh_id are a sequence id instead?!
  };
  uint32_t flags;
  int16_t parent_bone;            // Parent bone ID or -1 if there is none.
  uint16_t submesh_id;            // Mesh part ID OR uDistToParent?
  union
  {                         // only >= BC?
    struct 
    {
      uint16_t uDistToFurthDesc;
      uint16_t uZRatioOfChain;
    } CompressData;               // No model has ever had this part of the union used.
    uint32_t boneNameCRC;         // these are for debugging only. their bone names match those in key bone lookup.
  };
  M2Track<glm::vec3> translation;
  M2Track<M2CompQuat> rotation;   // compressed values, default is (32767,32767,32767,65535) == (0,0,0,1) == identity
  M2Track<glm::vec3> scale;
  glm::vec3 pivot;                // The pivot point of that bone.
};

struct M2TextureTransform
{
  M2Track<glm::vec3> translation; // ( glm::vec3)
  M2Track<M2CompQuat> rotation; // (QuatS)
  M2Track<glm::vec3> scaling; // ( glm::vec3)
};

struct M2Vertex
{
  glm::vec3 pos;
  uint8_t bone_weights[4];
  uint8_t bone_indices[4];
  glm::vec3 normal;
  glm::vec2 tex_coords[2];  // two textures, depending on shader used
};

struct M2SkinSection
{
  uint16_t skinSectionId;       // Mesh part ID, see below.
  uint16_t Level;               // (level << 16) is added (|ed) to startTriangle and alike to avoid having to increase those fields to uint32s.
  uint16_t vertexStart;         // Starting vertex number.
  uint16_t vertexCount;         // Number of vertices.
  uint16_t indexStart;          // Starting triangle index (that's 3* the number of triangles drawn so far).
  uint16_t indexCount;          // Number of triangle indices.
  uint16_t boneCount;           // Number of elements in the bone lookup table. Max seems to be 256 in Wrath. Shall be != 0.
  uint16_t boneComboIndex;      // Starting index in the bone lookup table.
  uint16_t boneInfluences;      // <= 4
                                // from <=BC documentation: Highest number of bones needed at one time in this Submesh --Tinyn (wowdev.org) 
                                // In 2.x this is the amount of of bones up the parent-chain affecting the submesh --NaK
                                // Highest number of bones referenced by a vertex of this submesh. 3.3.5a and suspectedly all other client revisions. -- Skarn
  uint16_t centerBoneIndex;
  glm::vec3 centerPosition;     // Average position of all the vertices in the sub mesh.
  glm::vec3 sortCenterPosition; // The center of the box when an axis aligned box is built around the vertices in the submesh.
  float sortRadius;             // Distance of the vertex farthest from CenterBoundingBox.
};

// same as M2SkinSection but with a uint32 as istart, to handle index > 65535 (present in HD models)
class M2SkinSectionHD
{
  public:
    M2SkinSectionHD():
      skinSectionId(-1), vertexStart(0), vertexCount(0),
      indexStart(0), indexCount(0), boneCount(0),
      boneComboIndex(0), boneInfluences(0), centerBoneIndex(0), 
      centerPosition(glm::vec3(0, 0, 0)), sortCenterPosition(glm::vec3(0, 0, 0)),
      sortRadius(0), display(false)
    {}

    M2SkinSectionHD(M2SkinSection & geo) :
      skinSectionId(geo.skinSectionId &0x7FFF), vertexStart(geo.vertexStart), vertexCount(geo.vertexCount),
      indexStart(geo.indexStart), indexCount(geo.indexCount), boneCount(geo.boneCount),
      boneComboIndex(geo.boneComboIndex), boneInfluences(geo.boneInfluences), centerBoneIndex(geo.centerBoneIndex),
      centerPosition(geo.centerPosition), sortCenterPosition(geo.sortCenterPosition),
      sortRadius(geo.sortRadius), display(false)
    {}

    M2SkinSectionHD(const M2SkinSectionHD & geo) :
      skinSectionId(geo.skinSectionId), vertexStart(geo.vertexStart), vertexCount(geo.vertexCount),
      indexStart(geo.indexStart), indexCount(geo.indexCount), boneCount(geo.boneCount),
      boneComboIndex(geo.boneComboIndex), boneInfluences(geo.boneInfluences), centerBoneIndex(geo.centerBoneIndex),
      centerPosition(geo.centerPosition), sortCenterPosition(geo.sortCenterPosition),
      sortRadius(geo.sortRadius), display(geo.display)
    {}

    uint16_t skinSectionId;       // Mesh part ID, see below.
    uint16_t Level;               // (level << 16) is added (|ed) to startTriangle and alike to avoid having to increase those fields to uint32s.
    uint16_t vertexStart;         // Starting vertex number.
    uint16_t vertexCount;         // Number of vertices.
    uint32_t indexStart;          // Starting triangle index (that's 3* the number of triangles drawn so far).
    uint16_t indexCount;          // Number of triangle indices.
    uint16_t boneCount;           // Number of elements in the bone lookup table. Max seems to be 256 in Wrath. Shall be != 0.
    uint16_t boneComboIndex;      // Starting index in the bone lookup table.
    uint16_t boneInfluences;      // <= 4
                                  // from <=BC documentation: Highest number of bones needed at one time in this Submesh --Tinyn (wowdev.org) 
                                  // In 2.x this is the amount of of bones up the parent-chain affecting the submesh --NaK
                                  // Highest number of bones referenced by a vertex of this submesh. 3.3.5a and suspectedly all other client revisions. -- Skarn
    uint16_t centerBoneIndex;
    glm::vec3 centerPosition;     // Average position of all the vertices in the sub mesh.
    glm::vec3 sortCenterPosition; // The center of the box when an axis aligned box is built around the vertices in the submesh.
    float sortRadius;             // Distance of the vertex farthest from CenterBoundingBox.
    bool display;
};


#define  TEXTUREUNIT_STATIC  0x10
#define  TEXTUREUNIT_IGNORE_TEXTURE_WEIGHTS  0x40
/// Lod part, A texture unit (sub of material)
struct M2Batch
{
  uint8_t flags;                       // Usually 16 for static textures, and 0 for animated textures. &0x1: materials invert something; &0x2: transform &0x4: projected texture; &0x10: something batch compatible; &0x20: projected texture?; &0x40: possibly don't multiply transparency by texture weight transparency to get final transparency value(?)
  int8_t priorityPlane;
  uint16_t shader_id;                  // See below.
  uint16_t skinSectionIndex;           // A duplicate entry of a submesh from the list above.
  uint16_t geosetIndex;                // See below. New name: flags2. 0x2 - projected. 0x8 - EDGF chunk in m2 is mandatory and data from is applied to this mesh
  uint16_t colorIndex;                 // A Color out of the Colors-Block or -1 if none.
  uint16_t materialIndex;              // The renderflags used on this texture-unit.
  uint16_t materialLayer;              // Capped at 7 (see CM2Scene::BeginDraw)
  uint16_t textureCount;               // 1 to 4. See below. Also seems to be the number of textures to load, starting at the texture lookup in the next field (0x10).
  uint16_t textureComboIndex;          // Index into Texture lookup table
  uint16_t textureCoordComboIndex;     // Index into the texture unit lookup table.
  uint16_t textureWeightComboIndex;    // Index into transparency lookup table.
  uint16_t textureTransformComboIndex; // Index into uvanimation lookup table. 
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

struct M2ShadowBatch
{
  uint8_t flags;              // if auto-generated: M2Batch.flags & 0xFF
  uint8_t flags2;             // if auto-generated: (renderFlag[i].flags & 0x04 ? 0x01 : 0x00)
                              //                  | (!renderFlag[i].blendingmode ? 0x02 : 0x00)
                              //                  | (renderFlag[i].flags & 0x80 ? 0x04 : 0x00)
                              //                  | (renderFlag[i].flags & 0x400 ? 0x06 : 0x00)
  uint16_t _unknown1;
  uint16_t submesh_id;
  uint16_t texture_id;        // already looked-up
  uint16_t color_id;
  uint16_t transparency_id;   // already looked-up
};

struct M2SkinProfile
{
  char id[4];         // Signature
  M2Array<uint16_t> vertices;
  M2Array<uint16_t> indices;
  M2Array<uint8_t>         bones;
  M2Array<M2SkinSection>  submeshes;
  M2Array<M2Batch>        batches;
  uint32_t                boneCountMax;                  // WoW takes this and divides it by the number of bones in each submesh, then stores the biggest one.
                                                         // Maximum number of bones per drawcall for each view. Related to (old) GPU numbers of registers. 
                                                         // Values seen : 256, 64, 53, 21
  M2Array<M2ShadowBatch>  shadow_batches;
};


// block X - render flags
/* flags */
#define  RENDERFLAGS_UNLIT  1
#define  RENDERFLAGS_UNFOGGED  2
#define  RENDERFLAGS_TWOSIDED  4
#define  RENDERFLAGS_BILLBOARD  8
#define  RENDERFLAGS_ZBUFFERED  16
struct ModelRenderFlags
{
  uint16_t flags;
  //unsigned char f1;
  //unsigned char f2;
  uint16_t blend; // see enums.h, enum BlendModes
};

// block G - color defs
// For some swirling portals and volumetric lights, these define vertex colors. 
// Referenced from the Texture Unit blocks in the LOD part. Contains a separate timeline for transparency values. 
// If no animation is used, the given value is constant.
struct M2Color
{
  M2Track<glm::vec3> color; // ( glm::vec3) Three floats. One for each color.
  M2Track<int16_t> alpha; // (UInt16) 0 - transparent, 0x7FFF - opaque.
};

// block H - transparency defs
struct M2TextureWeight
{
  M2Track<int16_t> weight; // (UInt16)
};

struct M2Texture
{
  uint32_t type;
  uint32_t flags;
  M2Array<char> filename; // for non-hardcoded textures (type != 0), this still points to a zero-byte-only string.
};

struct M2Light
{
  uint16_t type; // 0: Directional, 1: Point light
  int16_t bone; // If its attached to a bone, this is the bone. Else here is a nice -1.
  glm::vec3 position; // Position, Where is this light?
  M2Track<glm::vec3> ambient_color; // ( glm::vec3) The ambient color. Three floats for RGB.
  M2Track<float> ambient_intensity; // (Float) A float for the intensity.
  M2Track<glm::vec3> diffuse_color; // ( glm::vec3) The diffuse color. Three floats for RGB.
  M2Track<float> diffuse_intensity; // (Float) A float for the intensity again.
  M2Track<float> attenuation_start; // (Float) This defines, where the light starts to be.
  M2Track<float> attenuation_end; // (Float) And where it stops.
  M2Track<uint8_t> visibility; // (Uint32) Its an integer and usually 1.
};

struct M2Camera
{
  uint32_t type; // 0: portrait, 1: characterinfo; -1: else (flyby etc.); referenced backwards in the lookup table.
  float far_clip;
  float near_clip;
  M2Track<M2SplineKey<glm::vec3> > positions; // How the camera's position moves. Should be 3*3 floats.
  glm::vec3 position_base;
  M2Track<M2SplineKey<glm::vec3> > target_position; // How the target moves. Should be 3*3 floats.
  glm::vec3 target_position_base;
  M2Track<M2SplineKey<float>> roll; // The camera can have some roll-effect. Its 0 to 2*Pi. 
  M2Track<M2SplineKey<float>> FoV; //Diagonal FOV in radians. See below for conversion.
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

struct M2Particle
{
  uint32_t particleId;                        // Always (as I have seen): -1.
  uint32_t flags;                             // See Below
  glm::vec3 position;                       // The position. Relative to the following bone.
  uint16_t bone;                              // The bone its attached to.
  union
  {
    uint16_t texture;                         // And the textures that are used. 
    struct                                  // For multi-textured particles actually three ids
    {
      uint16_t texture_0 : 5;
      uint16_t texture_1 : 5;
      uint16_t texture_2 : 5;
      uint16_t : 1;
    };
  };
  M2Array<char> geometry_model_filename;    // if given, this emitter spawns models
  M2Array<char> recursion_model_filename;   // if given, this emitter is an alias for the (maximum 4) emitters of the given model

  uint8_t blendingType;                       // A blending type for the particle. See Below
  uint8_t emitterType;                        // 1 - Plane (rectangle), 2 - Sphere, 3 - Spline, 4 - Bone
  uint16_t particleColorIndex;                // This one is used for ParticleColor.dbc. See below.

  fp_2_5 multiTextureParamX[2];

  uint16_t textureTileRotation;               // Rotation for the texture tile. (Values: -1,0,1) -- priorityPlane
  uint16_t textureDimensions_rows;            // for tiled textures
  uint16_t textureDimensions_columns;
  M2Track<float> emissionSpeed;             // Base velocity at which particles are emitted.
  M2Track<float> speedVariation;            // Random variation in particle emission speed. (range: 0 to 1)
  M2Track<float> verticalRange;             // longitude; Drifting away vertically. (range: 0 to pi) For plane generators, this is the maximum polar angle of the initial velocity; 
                                            // 0 makes the velocity straight up (+z). For sphere generators, this is the maximum elevation of the initial position; 
                                            // 0 makes the initial position entirely in the x-y plane (z=0).
  M2Track<float> horizontalRange;           // latitude; They can do it horizontally too! (range: 0 to 2*pi) For plane generators, this is the maximum azimuth angle of the initial velocity; 
                                            // 0 makes the velocity have no sideways (y-axis) component. 
                                            // For sphere generators, this is the maximum azimuth angle of the initial position.
  M2Track<float> gravity;                   // Not necessarily a float; see below.
  M2Track<float> lifespan;                  // Number of seconds each particle continues to be drawn after its creation.[1]
  float lifespanVary;                       // An individual particle's lifespan is added to by lifespanVary * random(-1, 1)
  M2Track<float> emissionRate;
  float emissionRateVary;                   // This adds to the base emissionRate value the same way as lifespanVary. The random value is different every update.

  M2Track<float> emissionAreaLength;        // For plane generators, this is the width of the plane in the x-axis.
                                            // For sphere generators, this is the minimum radius.
  M2Track<float> emissionAreaWidth;         // For plane generators, this is the width of the plane in the y-axis.
                                            // For sphere generators, this is the maximum radius.
  M2Track<float> zSource;                   // When greater than 0, the initial velocity of the particle is (particle.position - C3Vector(0, 0, zSource)).Normalize()

  FBlock<glm::vec3> colorTrack;             // Most likely they all have 3 timestamps for {start, middle, end}.
  FBlock<fixed16> alphaTrack;
  FBlock<glm::vec2> scaleTrack;
  glm::vec3 scaleVary;                      // A percentage amount to randomly vary the scale of each particle
  FBlock<uint16_t> headCellTrack;             // Some kind of intensity values seen: 0,16,17,32 (if set to different it will have high intensity)
  FBlock<uint16_t> tailCellTrack;

  float tailLength;                         // A multiplier to the calculated tail particle length.[1]
  float twinkleSpeed;                       // twinkleFPS; has something to do with the spread
  float twinklePercent;                     // same mechanic as MDL twinkleOnOff but non-binary in 0.11.0
  CRange twinkleScale;                     // min, max
  float burstMultiplier;                    // ivelScale; requires (flags & 0x40)
  float drag;                               // For a non-zero values, instead of travelling linearly the particles seem to slow down sooner. Speed is multiplied by exp( -drag * t ).

  float baseSpin;                           // Initial rotation of the particle quad
  float baseSpinVary;
  float spin;                               // Rotation of the particle quad per second
  float spinVary;

  M2Box tumble;
  glm::vec3 windVector;
  float windTime;

  float followSpeed1;
  float followScale1;
  float followSpeed2;
  float followScale2;
  M2Array<glm::vec3> splinePoints;                                  // Set only for spline praticle emitter. Contains array of points for spline
  M2Track<unsigned char> enabledIn;                 // (boolean) Appears to be used sparely now, probably there's a flag that links particles to animation sets where they are enabled.
  vector_2fp_6_9 multiTextureParam0[2];
  vector_2fp_6_9 multiTextureParam1[2];
};

struct M2Ribbon
{
  uint32_t ribbonId;                  // Always (as I have seen): -1.
  uint32_t boneIndex;                 // A bone to attach to.
  glm::vec3 position;                 // And a position, relative to that bone.
  M2Array<uint16_t> textureIndices;   // into textures
  M2Array<uint16_t> materialIndices;  // into materials
  M2Track<glm::vec3> colorTrack;      // An RGB multiple for the material.[1]
  M2Track<fixed16> alphaTrack;       // And an alpha value in a short, where: 0 - transparent, 0x7FFF - opaque.
  M2Track<float> heightAboveTrack;    // Above and Below – These fields define the width of a ribbon in units based on their offset from the origin.[1]
  M2Track<float> heightBelowTrack;    // do not set to same!
  float edgesPerSecond;               // this defines how smooth the ribbon is. A low value may produce a lot of edges. 
                                      // Edges/Sec – The number of quads generated.[1]
  float edgeLifetime;                 // the length aka Lifespan. in seconds 
                                      // Time in seconds that the quads stay around after being generated.[1]
  float gravity;                      // use arcsin(val) to get the emission angle in degree 
                                      // Can be positive or negative. Will cause the ribbon to sink or rise in the z axis over time.[1]
  uint16_t textureRows;               // tiles in texture
  uint16_t textureCols;               // Texture Rows and Cols – Allows an animating texture similar to BlizParticle. Set the number of rows and columns equal to the texture.[1]
  M2Track<uint16_t> texSlotTrack;     // Pick the index number of rows and columns, and animate this number to get a cycle.[1]
  M2Track<unsigned char> visibilityTrack;
  int16_t priorityPlane;
  uint16_t padding;
};

/* 
These events are used for timing sounds for example. You can find the $DTH (death) event on nearly every model. It will play the death sound for the unit.
The events you can use depend on the way, the model is used. Dynamic objects can shake the camera, doodads shouldn't. Units can do a lot more than other objects.
Somehow there are some entries, that don't use the $... names but identifiers like "DEST" (destination), "POIN" (point) or "WHEE" (wheel). How they are used? Idk.
*/
struct M2Event
{
  char id[4]; // This is a (actually 3 character) name for the event with a $ in front.
  uint32_t data;        // This data is passed when the event is fired. 
  uint32_t bone;        // Somewhere it has to be attached.
  glm::vec3 position;   // Relative to that bone of course, animated. Pivot without animating.
  M2TrackBase enabled;  // This is a timestamp-only animation block. It is built up the same as a normal AnimationBlocks, but is missing values, as every timestamp is an implicit "fire now"
};

/*
 * This block specifies a bunch of locations on the body - hands, shoulders, head, back, 
 * knees etc. It is used to put items on a character. This seems very likely as this block 
 * also contains positions for sheathed weapons, a shield, etc.
 */
struct M2Attachment
{
  uint32_t id;                      // Referenced in the lookup-block below.
  uint16_t bone;                    // attachment base
  uint16_t unknown;                 // see BogBeast.m2 in vanilla for a model having values here
  glm::vec3 position;               // relative to bone; Often this value is the same as bone's pivot point 
  M2Track<unsigned char> animate_attached;  // whether or not the attached model is animated when this model is. only a bool is used. default is true.
};


struct ModelHeader
{
  char id[4];
  uint8_t version[4];
  M2Array<char> name;

  struct
  {
    uint32_t flag_tilt_x : 1;
    uint32_t flag_tilt_y : 1;
    uint32_t : 1;
    uint32_t flag_use_texture_combiner_combos : 1;      // add textureCombinerCombos array to end of data
    uint32_t : 1;
    uint32_t flag_load_phys_data : 1;
    uint32_t : 1;
    uint32_t flag_unk_0x80 : 1;                         // with this flag unset, demon hunter tattoos stop glowing
                                                                                         // since Cata (4.0.1.12911) every model now has this flag
    uint32_t flag_camera_related : 1;                   // TODO: verify version
    uint32_t flag_new_particle_record : 1;              // In CATA: new version of ParticleEmitters. By default, length of M2ParticleOld is 476. 
                                                                                         // But if 0x200 is set or if version is bigger than 271, length of M2ParticleOld is 492.
    uint32_t flag_unk_0x400 : 1;
    uint32_t flag_texture_transforms_use_bone_sequences : 1; // When set, texture transforms are animated using the sequence being played on the bone found by index in tex_unit_lookup_table[textureTransformIndex], instead of using the sequence being played on the model's first bone. Example model: 6DU_HellfireRaid_FelSiege03_Creature
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
  } global_flags;

  M2Array<M2Loop> global_loops;                         // Timestamps used in global looping animations.
  M2Array<M2Sequence> sequences;                        // Information about the animations in the model.
  M2Array<uint16_t> sequenceIdxHashById;                // Mapping of sequence IDs to the entries in the Animation sequences block.

  M2Array<M2CompBone> bones;                           // MAX_BONES = 0x100 => Creature\SlimeGiant\GiantSlime.M2 has 312 bones
  M2Array<uint16_t> boneIndicesById;                   //Lookup table for key skeletal bones. (alt. name: key_bone_lookup)

  M2Array<M2Vertex> vertices;
  uint32_t num_skin_profiles;                           // Views (LOD) are now in .skins.

  M2Array<M2Color> colors;                             // Color and alpha animations definitions.
  M2Array<M2Texture> textures;
  M2Array<M2TextureWeight> texture_weights;            // Transparency of textures.
  M2Array<M2TextureTransform> texture_transforms;
  M2Array<uint16_t> textureIndicesById;                // (alt. name: replacable_texture_lookup)
  M2Array<M2Material> materials;                       // Blending modes / render flags.
  M2Array<uint16_t> boneCombos;                        // (alt. name: bone_lookup_table)
  M2Array<uint16_t> textureCombos;                     // (alt. name: texture_lookup_table)
  M2Array<uint16_t> textureTransformBoneMap;           // (alt. name: tex_unit_lookup_table)
  M2Array<uint16_t> textureWeightCombos;               // (alt. name: transparency_lookup_table)
  M2Array<uint16_t> textureTransformCombos;            // (alt. name: texture_transforms_lookup_table)

  CAaBox bounding_box;                                  // min/max( [1].z, 2.0277779f ) - 0.16f seems to be the maximum camera height
  float bounding_sphere_radius;                         // detail doodad draw dist = clamp (bounding_sphere_radius * detailDoodadDensityFade * detailDoodadDist, …)
  CAaBox collision_box;
  float collision_sphere_radius;

  M2Array<uint16_t> collisionIndices;                    // (alt. name: collision_triangles)
  M2Array<glm::vec3> collisionPositions;                 // (alt. name: collision_vertices)
  M2Array<glm::vec3> collisionFaceNormals;               // (alt. name: collision_normals) 
  M2Array<M2Attachment> attachments;                     // position of equipped weapons or effects
  M2Array<uint16_t> attachmentIndicesById;               // (alt. name: attachment_lookup_table)
  M2Array<M2Event> events;                               // Used for playing sounds when dying and a lot else.
  M2Array<M2Light> lights;                               // Lights are mainly used in loginscreens but in wands and some doodads too.
  M2Array<M2Camera> cameras;                             // The cameras are present in most models for having a model in the character tab. 
  M2Array<uint16_t> cameraIndicesById;                   // (alt. name: camera_lookup_table)
  M2Array<M2Ribbon> ribbon_emitters;                     // Things swirling around. See the CoT-entrance for light-trails.
  M2Array<M2Particle> particle_emitters;
  M2Array<uint16_t> textureCombinerCombos;               // When set, textures blending is overriden by the associated array.
};

#pragma pack(pop)
#endif