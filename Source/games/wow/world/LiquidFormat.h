/*
 * LiquidFormat.h  (partial port of Tools/UnityRendererProject/Assets/Scripts/Wow/World/LiquidFormat.cs)
 *
 * Only what AdtParser needs for the MH2O vertex formats: the format constants and sizes, the depth scales and
 * the format rule (Decide). The C# file's catalogue rows (LiquidTypeView, LiquidCatalog) and the mesh arrays
 * (LiquidSurface, LiquidSurfaceBuilder) are not ported here; ILiquidCatalog is reduced to the one column the
 * rule reads (the LiquidMaterial LVF).
 *
 * Evidence (docs/world-viewer/09-liquids.md 1.2 and 1.4):
 *   - Vertex formats, by the bytes per vertex of an MH2O vertex block (PROVEN_BYTES, every instance of the four
 *     clients' catalogued maps); n = (Width + 1)(Height + 1) vertices, each array in vertex order:
 *         0: f32 height[n], u8 depth[n]                        5 bytes per vertex
 *         1: f32 height[n], (u16 u, u16 v)[n]                  8
 *         2: u8 depth[n]; the height is the instance's min     1
 *         3: f32 height[n], (u16 u, u16 v)[n], u8 depth[n]     9
 *   - The format rule, matching 6,334,582 of 6,334,582 instances (PROVEN_BYTES): the instance's second word when
 *     it is 0, 1 or 2; otherwise 2 for liquid type 2 ("Ocean", which has depth-only data or none -- why that type
 *     is special is UNKNOWN); otherwise the LVF of the type's LiquidMaterial.
 *   - The depth byte is a quantised depth -- the liquid vertex's height above the terrain's outer vertex under it --
 *     on one of two scales, picked by the instance's second word, not by the liquid type (PROVEN_BYTES: a census
 *     of 15,384 Retail, 839 Era and 2,022 Beta roots, 99.5% of vertices and more): word 2 or 42, the O scale,
 *     byte = clamp(ceil(12/7 D), 0, 255); any other word (0, or a LiquidObject id), the W scale, byte =
 *     clamp(floor(9 D), 0, 255). MLIQ's byte is the W scale. DepthYards decodes a byte to the middle of its step
 *     (or 0). What a stored u/v measures, and on what scale, is UNKNOWN (09-liquids.md 1.2).
 */

#ifndef _WMV_WORLD_LIQUIDFORMAT_H_
#define _WMV_WORLD_LIQUIDFORMAT_H_

#include <cstdint>

#include "AdtModel.h"
#include "WorldApi.h"

namespace wmv
{
namespace world
{
/// The part of the host's liquid catalogue the format rule reads.
class ILiquidCatalog
{
public:
  virtual ~ILiquidCatalog() = default;
  /// The LiquidMaterial.LVF (0..3) of a LiquidType id; false when the client's table has no such id, the type has
  /// no material row, or the catalogue is unavailable.
  virtual bool TryGetLvf(int liquidTypeId, int& lvf) const = 0;
};

/// Where a format decision came from (LiquidFormat::Decide).
enum class LiquidFormatSource
{
  /// No rule applied and the instance has no vertex data.
  None,
  /// The instance's second word is 0, 1 or 2: the format itself.
  Word,
  /// Liquid type 2 ("Ocean"): format 2.
  OceanType,
  /// The LVF of the type's LiquidMaterial, from the catalogue.
  Catalog,
  /// The catalogue lacks the type (or its material): the vertex block's size.
  Size,
};

struct LiquidFormatDecision
{
  int Format = -1; // 0..3, or -1
  LiquidFormatSource Source = LiquidFormatSource::None;
  /// The rule and the vertex block's size both give a format, and they differ (never in a client file). The
  /// decoded arrays follow the size (the bytes); the caller reports the disagreement.
  bool Disagrees = false;
};

class _WMVWORLD_API_ LiquidFormat
{
public:
  static constexpr int None = -1, HeightDepth = 0, HeightUv = 1, DepthOnly = 2, HeightUvDepth = 3;
  /// The liquid type whose instances are depth-only or flat whatever its material says ("Ocean").
  static constexpr int OceanType = 2;

  /// The single scale applied to stored texture coordinates (MH2O u16 u/v, MLIQ i16 s/t), whose own scale is
  /// UNKNOWN (09-liquids.md 1.2, 1.3: MH2O steps about 42.5 per quad and wraps at 65536). 1/256 is a documented
  /// choice, to be checked against the in-game references; 65536 x 1/256 is a whole number, so a wrap of the
  /// stored value does not move the texture.
  static constexpr float StoredUvScale = 1.0f / 256.0f;

  static int BytesPerVertex(int format);
  /// The format of a vertex block of `bytes` per vertex, -1 for any other size.
  static int FormatOfBytesPerVertex(int bytes);
  static bool HasHeights(int format) { return format == HeightDepth || format == HeightUv || format == HeightUvDepth; }
  static bool HasUv(int format) { return format == HeightUv || format == HeightUvDepth; }
  static bool HasDepth(int format) { return format == HeightDepth || format == DepthOnly || format == HeightUvDepth; }

  /// Whether an MH2O instance's depth bytes are on the O scale: its second word is 2 or 42 (PROVEN_BYTES, see the
  /// file header); every other word is the W scale.
  static bool DepthScaleO(uint16_t objectOrFormat) { return objectOrFormat == 2 || objectOrFormat == 42; }
  /// W scale (byte = clamp(floor(9 D), 0, 255)): the middle of the byte's step, (b + 0.5) / 9 yards.
  static double DepthYardsW(uint8_t b) { return (b + 0.5) / 9.0; }
  /// O scale (byte = clamp(ceil(12/7 D), 0, 255)): 0 for byte 0, else the middle of its step, (b - 0.5) x 7/12 yards.
  static double DepthYardsO(uint8_t b) { return b == 0 ? 0.0 : (b - 0.5) * 7.0 / 12.0; }
  /// An MH2O depth byte in yards, on the scale its instance's second word picks.
  static double DepthYards(uint8_t b, uint16_t objectOrFormat)
  {
    return DepthScaleO(objectOrFormat) ? DepthYardsO(b) : DepthYardsW(b);
  }

  /// The format rule (09-liquids.md 1.2) for an MH2O instance: the second word if it is 0, 1 or 2; else 2 for
  /// liquid type 2; else the catalogue's LVF for the type; else (no catalogue row, or no material) the instance's
  /// FormatBySize. Disagrees is set when the rule and the size both give a format and they differ. `catalog` may
  /// be null.
  static LiquidFormatDecision Decide(const LiquidInstance& instance, const ILiquidCatalog* catalog);

  /// Decide's format: 0..3, or -1 when no rule applies and the instance has no vertex data.
  static int Resolve(const LiquidInstance& instance, const ILiquidCatalog* catalog) { return Decide(instance, catalog).Format; }
};
} // namespace world
} // namespace wmv

#endif
