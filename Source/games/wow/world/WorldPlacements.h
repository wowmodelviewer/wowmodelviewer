/*
 * WorldPlacements.h  (partial port of Tools/UnityRendererProject/Assets/Scripts/Wow/World/WorldPlacements.cs)
 *
 * Only the placement RECORDS AdtParser and WdtParser read (WorldFloat3, PlacementNameKind, PlacementSource,
 * DoodadPlacement, WmoPlacement, DoodadWorldBox, PlacementRecords). The C# file's placement transform
 * (WorldPoint, PlacementFrame, PlacementTransform) is not ported here.
 *
 * Field evidence (docs/world-viewer/00-audit.md):
 *   MDDF / MLDD, 36 bytes (5.6): u32 NameId @0 (a FileDataID on every record of the four clients; every record
 *     carries flag 0x40, so 0x40 = "FileDataID" is a 100% correlation, not an isolated meaning), u32 UniqueId @4,
 *     f32 x3 Position @8 (file frame, 6.3), f32 x3 Rotation @20 (degrees, 6.4), u16 Scale @32 (u16/1024, proven by
 *     the obj1 MLDX boxes, 6.4), u16 Flags @34 (only 0x40 correlated). MLDD holds the same placements as obj0 MDDF,
 *     matched by uniqueId (same name, position, rotation, scale; flags gain 0x20/0x08/0x10) -- NOT by index: the
 *     order differs on Retail 29_48 and 30_48. MLDX[i] belongs to MLDD[i], never to MDDF[i].
 *   MODF, 64 bytes (4.3, 5.6, 6.4): u32 NameId @0 (a FileDataID when flag 0x8 -- every ADT MODF and the modern WDT
 *     form; the old WDT form has flag 0 and names an MWMO entry), u32 UniqueId @4, f32 x3 Position @8, f32 x3
 *     Rotation @20, f32 x3 ExtentsMin @32 and f32 x3 ExtentsMax @44 (file frame; they are the exact world box: 201
 *     of 202 Retail placements to 0.1 yd, 6.4), u16 Flags @56, u16 @58 and u16 @60 (kept raw: the Python reader
 *     calls them doodad set / name set, not verified, U15), u16 Scale @62 (1024 on every ADT MODF; 0 on the old
 *     WDT form).
 *   MLDX, 28 bytes per MLDD record (5.6, 6.4): f32 x6 = the placed doodad's world box (min, max, world frame),
 *     then a 7th f32 kept raw (meaning not established).
 */

#ifndef _WMV_WORLD_WORLDPLACEMENTS_H_
#define _WMV_WORLD_WORLDPLACEMENTS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "WorldApi.h"
#include "WorldChunks.h"

namespace wmv
{
namespace world
{
/// Three floats exactly as stored (file frame or degrees, see the record). x, y, z are the C# A, B, C: the
/// stored order, NOT a converted vector.
using WorldFloat3 = glm::vec3;

/// What a placement's NameId refers to, as far as the audited data establishes.
enum class PlacementNameKind
{
  /// A FileDataID (MDDF flag 0x40 / MODF flag 0x8 -- true of every record in the audited clients).
  FileDataId,
  /// Not a FileDataID: an index or byte offset into a name table (MWMO in an old WMO-only WDT); which of the two
  /// cannot be told from the audited data.
  NameTableEntry,
};

/// Where a placement record was read: lets a later scene builder decide which records with the same uniqueId are
/// the same world instance, instead of the parser discarding any.
struct PlacementSource
{
  int TileX = 0, TileY = 0; // -1 for a WDT record
  std::string Container;    // "obj0 MDDF", "obj1 MLDD", "obj0 MODF", "WDT MODF"
  int RecordIndex = 0;      // index within that chunk
};

/// Bit-exact float comparison of two stored triples.
_WMVWORLD_API_ bool SameBits(const WorldFloat3& a, const WorldFloat3& b);

/// MDDF / MLDD record (36 bytes).
struct DoodadPlacement
{
  uint32_t NameId = 0;
  uint32_t UniqueId = 0;
  WorldFloat3 Position{0.0f}; // file frame
  WorldFloat3 Rotation{0.0f}; // degrees: a0, a1, a2 as stored
  uint16_t ScaleRaw = 0;      // scale = ScaleRaw / 1024
  uint16_t Flags = 0;
  PlacementSource Source;

  PlacementNameKind NameKind() const
  {
    return (Flags & 0x40) != 0 ? PlacementNameKind::FileDataId : PlacementNameKind::NameTableEntry;
  }

  double Scale() const { return ScaleRaw / 1024.0; }

  /// True when name, position, rotation and scale are identical (the audit's test for "the same placement
  /// repeated in another tile"); flags are compared separately because MLDD copies gain bits.
  bool SamePlacementAs(const DoodadPlacement& o) const
  {
    return NameId == o.NameId && UniqueId == o.UniqueId && SameBits(Position, o.Position) &&
           SameBits(Rotation, o.Rotation) && ScaleRaw == o.ScaleRaw;
  }
};

/// MODF record (64 bytes), in an obj0 file or a WMO-only WDT.
struct WmoPlacement
{
  uint32_t NameId = 0;
  uint32_t UniqueId = 0;
  WorldFloat3 Position{0.0f};   // file frame
  WorldFloat3 Rotation{0.0f};   // degrees
  WorldFloat3 ExtentsMin{0.0f}; // file frame, exact world box (6.4)
  WorldFloat3 ExtentsMax{0.0f};
  uint16_t Flags = 0;
  uint16_t Word58 = 0;   // raw (called doodad set by the Python reader; not verified, U15)
  uint16_t Word60 = 0;   // raw (called name set by the Python reader; not verified)
  uint16_t ScaleRaw = 0; // 1024 on every ADT MODF; 0 on the old WDT form
  PlacementSource Source;

  PlacementNameKind NameKind() const
  {
    return (Flags & 0x8) != 0 ? PlacementNameKind::FileDataId : PlacementNameKind::NameTableEntry;
  }

  /// ScaleRaw / 1024 when flag 0x4 is set (every ADT MODF: always 1024, so 1.0), else 1.0 -- the old WDT form
  /// stores 0 without the flag; 1.0 there is INFERRED (audit 4.3).
  double Scale() const { return (Flags & 0x4) != 0 ? ScaleRaw / 1024.0 : 1.0; }

  bool SamePlacementAs(const WmoPlacement& o) const
  {
    return NameId == o.NameId && UniqueId == o.UniqueId && SameBits(Position, o.Position) &&
           SameBits(Rotation, o.Rotation) && SameBits(ExtentsMin, o.ExtentsMin) && SameBits(ExtentsMax, o.ExtentsMax) &&
           Flags == o.Flags && Word58 == o.Word58 && Word60 == o.Word60 && ScaleRaw == o.ScaleRaw;
  }
};

/// MLDX record (28 bytes): the placed doodad's world box and a 7th value kept raw.
struct DoodadWorldBox
{
  WorldFloat3 Min{0.0f}; // world frame (X, Y, Z)
  WorldFloat3 Max{0.0f};
  float Value6Raw = 0.0f; // meaning not established
};

class _WMVWORLD_API_ PlacementRecords
{
public:
  static constexpr int DoodadRecordSize = 36;
  static constexpr int WmoRecordSize = 64;
  static constexpr int DoodadBoxRecordSize = 28;

  /// Every record as stored. Throws WorldParseException when the chunk is not a whole number of records or a
  /// position / rotation is non-finite.
  static std::vector<DoodadPlacement> ReadDoodads(const WorldSpan& s, int tileX, int tileY, const std::string& container);
  /// As ReadDoodads; extents must be finite too.
  static std::vector<WmoPlacement> ReadWmos(const WorldSpan& s, int tileX, int tileY, const std::string& container);
  /// Min/max must be finite; the 7th value is kept raw.
  static std::vector<DoodadWorldBox> ReadDoodadBoxes(const WorldSpan& s);
};
} // namespace world
} // namespace wmv

#endif
