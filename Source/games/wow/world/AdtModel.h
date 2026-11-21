/*
 * AdtModel.h  (port of Tools/UnityRendererProject/Assets/Scripts/Wow/World/AdtModel.cs)
 *
 * Result types of the split-ADT parsers (AdtParser.h). A terrain tile is up to five files named by MAID: root,
 * tex0, obj0, obj1, lod (audit 5.1); each part is parsed and reported on its own, so a missing or malformed tex0
 * never makes the root look corrupt.
 *
 * Field evidence is cited per field below (section numbers of docs/world-viewer/00-audit.md). Fields whose
 * meaning the audit did not establish are kept raw with a "Raw" suffix and are never interpreted. Every array
 * here is owned by the result object; no result keeps a reference to the caller's input buffer.
 *
 * C++ notes: a C# array that is null in a meaningful way is a std::optional<std::vector> here, or an empty
 * vector where the C# array is never empty when present (noted per field).
 */

#ifndef _WMV_WORLD_ADTMODEL_H_
#define _WMV_WORLD_ADTMODEL_H_

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "TerrainLayout.h"
#include "WorldApi.h"
#include "WorldChunks.h"
#include "WorldPlacements.h"

namespace wmv
{
namespace world
{
// ===================================================================================================== root

/// One root MCNK (128-byte header + sub-chunks). Offsets are relative to the MCNK chunk.
struct AdtRootChunk
{
  /// Position of this MCNK among the root's MCNKs (0..255).
  int Index = 0;
  WorldDataStatus Status = WorldDataStatus::Absent; // Decoded, or Malformed (see the root's issues)

  // ---- header (5.2); offsets proven by what each field equals in all 33,280 audited chunks
  std::vector<uint8_t> RawHeader; // the full 128 bytes, for fields not named here (empty: header not read)
  uint32_t Flags = 0;             // +0: raw; 0x40 <=> MCCV present, 0x1 <=> tex0 MCSH present,
                                  //     0x10000 = high-resolution hole mask (all 100% correlations)
  uint32_t IndexX = 0;            // +4: == Index % 16 in 100% of chunks
  uint32_t IndexY = 0;            // +8: == Index / 16
  uint32_t AreaId = 0;            // +52: an AreaTable id (resolved per client in the audit)
  WorldFloat3 StoredPosition{0.0f}; // +104: float32 as stored; x/y within 1/768 yd (0.0013021) of the index
                                    //       formula (6.1) -- use TerrainCoordinates for x/y
  TerrainHoles Holes;             // +20 (64-bit, flag 0x10000) / +60 (16-bit, not decoded)

  // ---- sub-chunks
  std::vector<float> Heights;          // MCVT, 145 floats relative to StoredPosition.z; empty = none usable
  std::vector<int8_t> NormalsRaw;      // MCNR, first 435 bytes as stored (145 triplets), kept raw; empty = none.
                                       //   (Stage 10 decodes them for the outdoor light: game (X, Y, Z) / 127,
                                       //   MEASURED against the geometry -- not done here)
  std::vector<uint8_t> NormalsTrailingRaw; // MCNR bytes after the 435 (13 in every audited chunk)
  std::vector<uint8_t> VertexColorsRaw;    // MCCV, 145 x 4 bytes as stored, empty = absent; channel order NOT
                                           //   established -- not decoded
  int McseSize = -1;                       // MCSE payload size (0 in every audited chunk), -1 = absent
  std::vector<WorldUnsupportedChunk> Unsupported; // MCBB, MPTX and anything else

  bool HasVertexColors() const { return !VertexColorsRaw.empty(); }
};

// MH2O evidence: docs/world-viewer/09-liquids.md 1.2 (every root ADT of the four clients' catalogued maps,
// 6,334,582 instances, read on 2026-10-18). Offsets inside MH2O count from the start of its payload.

/// MH2O per-chunk header (12 bytes); entry n belongs to MCNK n. An empty entry is all zero (PROVEN_BYTES).
struct LiquidChunkHeader
{
  uint32_t InstancesOffset = 0;  // +0: the chunk's LayerCount 24-byte instances
  uint32_t LayerCount = 0;       // +4: 1..4 in the clients
  uint32_t AttributesOffset = 0; // +8: 16-byte LiquidAttributes, 0 = none
};

/// A chunk's 16-byte attribute block: two 64-bit cell masks, cell (row, col) = bit row*8 + col, least significant
/// first; the second is a subset of the first in 100% of chunks (PROVEN_BYTES, layout). The names are public
/// documentation's (INFERRED); nothing here uses them.
struct LiquidAttributes
{
  bool Present = false; // false when the chunk has none, or its block was malformed
  uint64_t Fishable = 0;
  uint64_t Deep = 0;
};

/// One MH2O instance (a liquid layer of one chunk): the 24-byte record as stored, then what its exists bitmap and
/// vertex data decode to. Instance-local grid: quad (row r, column c), r < Height, c < Width, lies on the chunk's
/// cell (Y + r, X + c); vertex (i, j), i <= Height, j <= Width, is the chunk's outer terrain vertex (row Y + i,
/// column X + j) and has index i * (Width + 1) + j (PROVEN_BYTES, seam tests). Only a Decoded instance has
/// geometry; a Malformed one says why in Problem and its arrays are empty.
struct LiquidInstance
{
  // ---- the 24-byte record, raw words kept for diagnostics
  uint16_t LiquidType = 0;     // +0: a LiquidType id (100%)
  uint16_t ObjectOrFormat = 0; // +2: 0, 1, 2 = the vertex format itself (Retail only); 42 on ocean-class liquid
                               //     (never a LiquidObject id); otherwise a LiquidObject id (not resolved here)
  float MinHeight = 0.0f;      // +4: absolute world height (the terrain's z frame), as stored
  float MaxHeight = 0.0f;      // +8: min <= max in 100%
  uint8_t X = 0;               // +12: first cell column (index_x direction)
  uint8_t Y = 0;               // +13: first cell row (index_y direction)
  uint8_t Width = 0;           // +14: columns, X + Width <= 8
  uint8_t Height = 0;          // +15: rows, Y + Height <= 8
  uint32_t ExistsOffset = 0;   // +16: exists bitmap, 0 = none (every quad exists)
  uint32_t VertexOffset = 0;   // +20: vertex data, 0 = none (flat at MinHeight)

  // ---- decoded
  int Chunk = 0;                                    // MCNK index 0..255
  int Layer = 0;                                    // position among the chunk's instances
  WorldDataStatus Status = WorldDataStatus::Absent; // Decoded, or Malformed (no geometry)
  std::string Problem;                              // why it is Malformed, else empty
  /// Bytes of the vertex block: the gap from VertexOffset to the next structure (or the end of MH2O); 0 without
  /// vertex data.
  int VertexBlockBytes = 0;
  /// The vertex format the block's size gives (LiquidFormat: 1, 5, 8 or 9 bytes per vertex = 2, 0, 1, 3), -1
  /// without vertex data. The four sizes are distinct, so the size decides.
  int FormatBySize = -1;
  /// Width * Height quads, row-major (quad r, c at r * Width + c): bit r * Width + c of the stored bitmap, least
  /// significant first, bits past Width * Height masked; every quad without a bitmap. Empty when not decoded.
  std::vector<bool> Exists;
  bool HasExistsBitmap = false;
  bool ExistsSpareBits = false;  // the stored bitmap set bits past Width * Height (masked off)
  std::vector<float> Heights;    // formats 0, 1, 3: absolute heights per vertex, else empty
  std::vector<uint8_t> Depths;   // formats 0, 2, 3: one byte per vertex (LiquidFormat::DepthYards), else empty
  std::vector<uint16_t> U, V;    // formats 1, 3: the stored texture coordinates (scale UNKNOWN), else empty

  int VertexCount() const { return (Width + 1) * (Height + 1); }

  /// Whether quad (row r, column c) of the instance exists (false outside the instance or when nothing was
  /// decoded).
  bool QuadExists(int r, int c) const
  {
    if (Exists.empty() || r < 0 || r >= Height || c < 0 || c >= Width)
      return false;
    return Exists[static_cast<size_t>(r * Width + c)];
  }

  /// The height of vertex (i, j): its stored height, or MinHeight for depth-only data and for an instance without
  /// vertex data (flat; MinHeight == MaxHeight in 100% of those).
  float HeightAt(int i, int j) const
  {
    if (i < 0 || i > Height || j < 0 || j > Width)
      throw std::out_of_range("i/j: liquid vertex row/column outside the instance");
    return !Heights.empty() ? Heights[static_cast<size_t>(i * (Width + 1) + j)] : MinHeight;
  }
};

struct AdtLiquids
{
  /// PartiallyDecoded when the header table and every instance record are in bounds (some meanings stay raw: the
  /// LiquidObject word, the depth and texture-coordinate scales); Malformed otherwise, with no instances. A
  /// malformed attribute block, bitmap or vertex block is reported per chunk or instance (root issues "MH2O chunk
  /// n ...") and leaves the rest decoded.
  WorldDataStatus Status = WorldDataStatus::Absent;
  int DataOffset = 0; // MH2O payload start in the root file
  int Size = 0;
  std::vector<LiquidChunkHeader> Headers;              // 256 (empty when MH2O is shorter than the table)
  std::vector<LiquidAttributes> Attributes;            // 256 (same)
  std::vector<std::vector<LiquidInstance>> Instances;  // per chunk, in layer order; empty when the chunk has none
  int MalformedInstances = 0;
  int MalformedAttributes = 0;
};

struct AdtRoot
{
  std::string What;
  int TileX = 0, TileY = 0;
  uint32_t Version = 0;
  std::vector<WorldChunk> Chunks;            // every top-level chunk, in file order
  std::vector<AdtRootChunk> Mcnk;            // 256
  std::optional<AdtLiquids> Liquids;         // nullopt when the root has no MH2O
  std::vector<WorldUnsupportedChunk> Unsupported; // MHDR, MFBO, MBMH/MBBB/MBMI/MBNV, ...
  std::vector<WorldIssue> Issues;
};

// ===================================================================================================== tex0

/// MCLY record (16 bytes, 5.4).
struct TerrainLayer
{
  uint32_t TextureIndex = 0; // +0: index into MDID (every audited index is in range)
  uint32_t Flags = 0;        // +4: raw; 0x100 on exactly the layers after the first (100%), 0x200 = compressed
                             //     alpha (proven by RLE ending at the next layer); other bits (0x1-0x80, 0x400)
                             //     UNKNOWN (U10)
  uint32_t AlphaOffset = 0;  // +8: byte offset into MCAL (proven: offsets tile MCAL exactly)
  uint32_t Word12Raw = 0;    // +12: meaning not established (the audit draft's "effect id" is a name only)
};

enum class AlphaStorage
{
  /// Flag 0x200: RLE, 4096 values of 0..255.
  Compressed,
  /// 4096 bytes, one value of 0..255 per texel.
  Uncompressed8,
  /// 2048 bytes, two texels per byte, low nibble first: values 0..15.
  Uncompressed4,
};

/// A decoded 64x64 alpha map, texel (row, col) = Samples[row*64 + col], rows along the vertex rows (5.4, 6.5).
/// 4-bit maps keep their 0..15 samples: how the client expands them to 8 bits is UNKNOWN (audit U19), so no
/// expansion is offered.
struct AlphaMap
{
  AlphaStorage Storage = AlphaStorage::Compressed;
  std::vector<uint8_t> Samples; // 4096
  int StoredBytes = 0;          // bytes this layer occupies in MCAL

  int BitDepth() const { return Storage == AlphaStorage::Uncompressed4 ? 4 : 8; }

  uint8_t Sample(int row, int col) const
  {
    if (row < 0 || row > 63 || col < 0 || col > 63)
      throw std::out_of_range("row/col: alpha texel row/column must be 0..63");
    return Samples[static_cast<size_t>(row * 64 + col)];
  }

  /// 8-bit values for material preparation. True for 8-bit maps (compressed or not): `values` points at this
  /// map's own Samples, not a copy. False for 4-bit maps, with the unresolved requirement in `reason` -- never a
  /// guessed scale.
  bool TryGetEightBit(const std::vector<uint8_t>*& values, std::string& reason) const
  {
    if (BitDepth() == 8)
    {
      values = &Samples;
      reason.clear();
      return true;
    }
    values = nullptr;
    reason = "4-bit alpha: the client's 4-bit to 8-bit expansion is not established (audit U19); "
             "Retail re-encodes these maps as ~16n+15, which is evidence of a conversion, not of the draw-time rule";
    return false;
  }
};

struct AdtTexChunk
{
  int Index = 0;
  WorldDataStatus Status = WorldDataStatus::Absent; // Decoded, or Malformed (alpha not usable; see issues)
  std::vector<TerrainLayer> Layers;                 // MCLY (empty when absent)
  /// Per layer when Status is Decoded: nullopt for layer 0 and for undecodable layers. Empty when not decoded.
  std::vector<std::optional<AlphaMap>> Alpha;
  std::optional<std::vector<uint8_t>> ShadowRaw; // MCSH (512 bytes) or nullopt; bit layout NOT established
  int AlphaSize = -1;                            // MCAL payload size, -1 = absent
  std::vector<WorldUnsupportedChunk> Unsupported;
};

struct AdtTex
{
  std::string What;
  uint32_t Version = 0;
  std::vector<WorldChunk> Chunks;
  std::optional<std::vector<uint8_t>> MampRaw; // MAMP payload; value meaning UNKNOWN (U12)
  std::vector<uint32_t> DiffuseTextureIds;     // MDID FileDataIDs
  std::vector<uint32_t> HeightTextureIds;      // MHID FileDataIDs (blending rule UNKNOWN, U11)
  std::optional<std::vector<uint8_t>> MtxpRaw; // MTXP payload (16 bytes per texture), fields UNKNOWN (U11)
  std::vector<AdtTexChunk> Mcnk;               // 256
  std::vector<WorldUnsupportedChunk> Unsupported;
  std::vector<WorldIssue> Issues;
};

// ===================================================================================================== obj0 / obj1 / lod

struct AdtObj0Chunk
{
  int Index = 0;
  std::optional<std::vector<uint32_t>> McrdRaw; // MCRD u32 list (counts measured only; meaning not established)
  std::optional<std::vector<uint32_t>> McrwRaw; // MCRW u32 list (same)
  std::vector<WorldUnsupportedChunk> Unsupported;
};

struct AdtObj0
{
  std::string What;
  uint32_t Version = 0;
  std::vector<WorldChunk> Chunks;
  std::vector<DoodadPlacement> Doodads; // MDDF, every record as stored (no de-duplication)
  std::vector<WmoPlacement> Wmos;       // MODF, every record as stored
  std::vector<AdtObj0Chunk> Mcnk;       // 256
  std::vector<WorldUnsupportedChunk> Unsupported; // MWDR/MWDS, MLMB, MMDX/MMID/MWMO/MWID, ...
  std::vector<WorldIssue> Issues;
};

struct AdtObj1
{
  std::string What;
  uint32_t Version = 0;
  std::vector<WorldChunk> Chunks;
  std::vector<DoodadPlacement> Doodads; // MLDD: obj0's MDDF placements, matched by uniqueId (order may differ, 5.6)
  std::optional<std::vector<DoodadWorldBox>> DoodadBoxes; // MLDX: box i belongs to MLDD record i; nullopt when
                                                          // absent or inconsistent
  std::vector<WorldUnsupportedChunk> Unsupported; // MLFD, MLDL, MDLI, MDLD, MLDB, MLMD, MLMX, MLMB, MWDR, MWDS
  std::vector<WorldIssue> Issues;
};

/// _lod.adt: no chunk's layout is established (U17): an inventory only.
struct AdtLod
{
  std::string What;
  std::vector<WorldChunk> Chunks;
  std::vector<WorldIssue> Issues;
};

// ===================================================================================================== tile

enum class AdtPartState
{
  /// The WDT names no file for this part (MAID word 0).
  NotNamed,
  /// Named, but the caller supplied no bytes (not resolved, not local, or not read), or an empty buffer (Error
  /// then says so).
  NotSupplied,
  Parsed,
  /// Bytes supplied but the part is malformed; Error says why. Other parts are unaffected.
  Malformed,
};

_WMVWORLD_API_ const char* ToString(AdtPartState s);

template <typename T>
struct AdtPartResult
{
  AdtPartState State = AdtPartState::NotSupplied;
  std::optional<T> Value;
  std::string Error;
};

/// A terrain tile assembled from whichever parts were supplied, each reported on its own.
struct AdtTile
{
  int TileX = 0, TileY = 0;
  AdtPartResult<AdtRoot> Root;
  AdtPartResult<AdtTex> Tex0;
  AdtPartResult<AdtObj0> Obj0;
  AdtPartResult<AdtObj1> Obj1;
  AdtPartResult<AdtLod> Lod;
};
} // namespace world
} // namespace wmv

#endif
