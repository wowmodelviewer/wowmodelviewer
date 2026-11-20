// Shared declarations of the viewport shaders. The C++ side mirrors these structs (VulkanViewport.cpp,
// GpuDrawData / GpuFrameData): std430 layout, so keep the two in step.

const int UV_SET0 = 0;
const int UV_SET1 = 1;
const int UV_SPHERE = 2;
const int UV_SET2 = 3;

const int FLAG_UNLIT = 1;
const int FLAG_TEXTURED = 2;
const int FLAG_ALPHA_TEST = 4;
const int FLAG_PARTICLE_MULTITEX = 8;
const int FLAG_SCREEN_SPACE = 16;
const int FLAG_TWO_SIDED = 32;

struct DrawData
{
  mat4 mvp;
  mat4 modelView;
  mat4 normalMatrix;  // inverse transpose of modelView (upper 3x3 used)
  mat4 texMatrix0;
  mat4 texMatrix1;
  vec4 color;         // material colour, .w the pass opacity
  vec4 emissive;
  ivec4 uvSource;     // per unit
  int pixelShader;    // < 0: the fixed-function path
  int egxBlendMode;
  int flags;
  int textureCount;
  float alphaRef;
  float combinerAlphaTest;
  float specularWeight;
  float envBoost;
  float metalSpec;
  float metalTight;
  float pad0;
  float pad1;
};

layout(std430, set = 0, binding = 0) readonly buffer Draws
{
  DrawData draws[];
};

layout(std140, set = 0, binding = 1) uniform Frame
{
  vec4 keyDirView;    // the key light's direction (towards the light), eye space
  vec4 lightLevels;   // x ambient, y key, z front fill, w ceiling of the light sum
  vec4 lightColor;    // rgb
  vec4 options;       // x: 1 = lighting off for the whole frame (masking)
} frame;

layout(push_constant) uniform PushConstants
{
  int drawIndex;
} pc;
