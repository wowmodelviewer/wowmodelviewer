#version 450
// Terrain chunks: position, the MCNR normal, the cell UV (0..8: u east, v south) and MCCV / 127 as the colour.
#include "terrain_common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv0;
layout(location = 3) in vec2 inUv1;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vCellUv;
layout(location = 2) out vec4 vColor;

void main()
{
  TerrainDrawData d = terrainDraws[pc.drawIndex];
  vNormal = normalize(mat3(d.normalMatrix) * inNormal);
  vCellUv = inUv0;
  vColor = inColor;
  gl_Position = d.mvp * vec4(inPos, 1.0);
}
