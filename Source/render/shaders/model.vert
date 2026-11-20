#version 450
// The viewport's one vertex shader. It does what OpenGL's fixed pipeline did for the archived viewport: transform
// by the modelview and projection, pass the eye-space normal on, and make the texture coordinates of each unit --
// a UV set, or GL_SPHERE_MAP texgen from the eye-space position and normal -- through that unit's texture matrix.

#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv0;
layout(location = 3) in vec2 inUv1;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec3 vNormal;   // eye space
layout(location = 1) out vec3 vEyePos;   // eye space
layout(location = 2) out vec2 vTc[4];    // per texture unit
layout(location = 6) out vec4 vColor;

vec2 sphereMap(vec3 eyePos, vec3 n)
{
  // OpenGL's GL_SPHERE_MAP: the reflection of the eye vector about the normal, onto the unit circle.
  vec3 u = normalize(eyePos);
  vec3 r = reflect(u, n);
  float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
  return vec2(r.x / m + 0.5, r.y / m + 0.5);
}

vec2 unitCoord(int source, vec3 eyePos, vec3 n)
{
  if (source == UV_SET1) return inUv1;
  if (source == UV_SPHERE) return sphereMap(eyePos, n);
  return inUv0; // UV_SET0, and UV_SET2 (which the GL combiner fed with set 0)
}

void main()
{
  DrawData d = draws[pc.drawIndex];
  vec4 eye = d.modelView * vec4(inPos, 1.0);
  vec3 n = normalize(mat3(d.normalMatrix) * inNormal);
  vEyePos = eye.xyz;
  vNormal = n;
  vColor = inColor;

  for (int u = 0; u < 4; u++)
  {
    vec2 tc = unitCoord(d.uvSource[u], eye.xyz, n);
    if (u == 0) tc = (d.texMatrix0 * vec4(tc, 0.0, 1.0)).xy;
    else if (u == 1) tc = (d.texMatrix1 * vec4(tc, 0.0, 1.0)).xy;
    vTc[u] = tc;
  }

  gl_Position = d.mvp * vec4(inPos, 1.0);
}
