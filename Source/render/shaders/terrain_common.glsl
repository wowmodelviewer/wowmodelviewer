// Shared declarations of the terrain shaders; the C++ side mirrors TerrainDrawData (VulkanViewport.cpp, std430).

struct TerrainDrawData
{
  mat4 mvp;
  mat4 modelView;
  mat4 normalMatrix;
  vec4 repeats[2];       // layers 0..3, 4..7
  vec4 phaseU[2];
  vec4 phaseV[2];
  vec4 heightScale[2];
  vec4 heightOffset[2];
  vec4 hasHeight[2];     // 1 / 0
  vec4 missing[2];       // 1: no texture (the checker)
  int layerCount;
  int useHeights;
  int hasMccv;
  int flags;             // 1 = unlit (the frame's masking)
};

layout(std430, set = 0, binding = 2) readonly buffer TerrainDraws
{
  TerrainDrawData terrainDraws[];
};

layout(std140, set = 0, binding = 1) uniform Frame
{
  vec4 keyDirView;
  vec4 lightLevels;
  vec4 lightColor;
  vec4 options;
} frame;

layout(push_constant) uniform PushConstants
{
  int drawIndex;
} pc;

float layerValue(vec4 lo, vec4 hi, int k) { return k < 4 ? lo[k] : hi[k - 4]; }
