#version 450
// The terrain chunk's texture material: the client's HEIGHT MIX over up to 8 layers (Wmv/TerrainTextured, ported
// operation for operation; the evidence is in docs/world-viewer/04-terrain-materials.md and world/TerrainMaterial):
//   a_k (k >= 1) from the alpha slices at uv / 8 (texel i centred at (i + 0.5) / 64, bilinear, clamp)
//   w_0 = 1 - sum a_k, w_k = a_k
//   layer k at s_k = uv * repeats_k / 8 + phase_k; a missing layer is a magenta/black checker, one square per 1/8 cell
//   H_k = 1 without MPHD 0x80, else height texture alpha at s_k * scale_k + offset_k (0 with no height texture)
//   p_k = max(0, w_k) H_k; m = max p_k; p_k *= 1 - saturate(m - p_k); S = sum p_k;
//   colour = sum (p_k / S) layer_k, or sum max(0, w_k) layer_k when S <= 0; times MCCV; saturated
// Then lit by the viewport's preview rig (the client's outdoor light and fog come with the world's light, stage 3d).
#include "terrain_common.glsl"

layout(set = 1, binding = 0) uniform texture2D tLayer[8];
layout(set = 1, binding = 1) uniform texture2D tHeight[8];
layout(set = 1, binding = 2) uniform texture2D tAlpha[2];
layout(set = 1, binding = 3) uniform sampler sRepeat;
layout(set = 1, binding = 4) uniform sampler sClamp;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vCellUv;
layout(location = 2) in vec4 vColor;

layout(location = 0) out vec4 outColor;

vec3 checker(vec2 uv)
{
  vec2 c = floor(uv * 8.0);
  return mod(c.x + c.y, 2.0) < 1.0 ? vec3(1.0, 0.0, 1.0) : vec3(0.0);
}

void main()
{
  TerrainDrawData d = terrainDraws[pc.drawIndex];
  const int n = clamp(d.layerCount, 0, 8);
  vec2 auv = vCellUv / 8.0;
  vec4 a14 = texture(sampler2D(tAlpha[0], sClamp), auv);
  vec4 a57 = texture(sampler2D(tAlpha[1], sClamp), auv);

  float w[8];
  float sumA = 0.0;
  for (int k = 1; k < 8; k++)
  {
    float a = k < n ? (k <= 4 ? a14[k - 1] : a57[k - 5]) : 0.0;
    w[k] = a;
    sumA += a;
  }
  w[0] = 1.0 - sumA;

  vec3 col[8];
  float p[8];
  float m = 0.0;
  for (int k = 0; k < 8; k++)
  {
    col[k] = vec3(0.0);
    p[k] = 0.0;
    if (k >= n)
      continue;
    vec2 s = vCellUv * layerValue(d.repeats[0], d.repeats[1], k) / 8.0 +
             vec2(layerValue(d.phaseU[0], d.phaseU[1], k), layerValue(d.phaseV[0], d.phaseV[1], k));
    if (layerValue(d.missing[0], d.missing[1], k) > 0.5)
      col[k] = checker(vCellUv);
    else
      col[k] = texture(sampler2D(tLayer[k], sRepeat), s).rgb;
    float H = 1.0;
    if (d.useHeights != 0)
    {
      float h = layerValue(d.hasHeight[0], d.hasHeight[1], k) > 0.5 ? texture(sampler2D(tHeight[k], sRepeat), s).a : 0.0;
      H = h * layerValue(d.heightScale[0], d.heightScale[1], k) + layerValue(d.heightOffset[0], d.heightOffset[1], k);
    }
    p[k] = max(0.0, w[k]) * H;
    m = max(m, p[k]);
  }

  float S = 0.0;
  for (int k = 0; k < 8; k++)
  {
    p[k] *= 1.0 - clamp(m - p[k], 0.0, 1.0);
    S += p[k];
  }
  vec3 colour = vec3(0.0);
  for (int k = 0; k < 8; k++)
    colour += (S > 0.0 ? p[k] / S : max(0.0, w[k])) * col[k];
  if (d.hasMccv != 0)
    colour *= vColor.rgb;
  colour = clamp(colour, 0.0, 1.0);

  if ((d.flags & 1) == 0 && frame.options.x < 0.5)
  {
    vec3 nrm = normalize(vNormal);
    float key = max(dot(nrm, frame.keyDirView.xyz), 0.0);
    float front = max(nrm.z, 0.0);
    float l = frame.lightLevels.x + frame.lightLevels.y * key + frame.lightLevels.z * front * (1.0 - key);
    colour *= frame.lightColor.rgb * min(l, frame.lightLevels.w);
  }
  outColor = vec4(colour, 1.0);
}
