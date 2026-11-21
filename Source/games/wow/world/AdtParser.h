/*
 * AdtParser.h  (port of Tools/UnityRendererProject/Assets/Scripts/Wow/World/AdtParser.cs)
 *
 * Bounds-safe readers for the split ADT parts (root, tex0, obj0, obj1, lod), from the bytes the caller supplies.
 * See AdtModel.h for the result types and per-field evidence; the rules below are the ones
 * docs/world-viewer/00-audit.md proved against the bytes of all four audited clients:
 *   - Every part is a flat chunk list (MVER first). A root has 256 MCNKs, each a 128-byte header then sub-chunks;
 *     tex0 and obj0 have 256 MCNKs whose sub-chunks start at their first byte; MCNK n of every part is the same
 *     terrain chunk (5.1).
 *   - In split files layers live only in tex0 (root n_layers and alpha/shadow offsets are 0).
 *   - MCAL (5.4): the layers after the first carry alpha maps, laid out back to back from offset 0 in layer
 *     order. A layer with MCLY flag 0x200 is RLE-compressed: a count byte n; top bit set = repeat the next byte
 *     (n & 0x7F) times, clear = copy the next (n & 0x7F) bytes; it ends at 4096 values, exactly where the next
 *     layer starts. An uncompressed layer occupies the gap to the next layer (or to the end of MCAL): 4096 bytes =
 *     8-bit, 2048 = 4-bit, low nibble first. The bit depth comes from that spacing, never from an MPHD flag.
 *   - MH2O (docs/world-viewer/09-liquids.md 1.2): a 256-entry header table, the instances, then the attributes,
 *     exists bitmaps and vertex data packed in chunk order; a vertex block's size (the gap to the next structure)
 *     gives its format (LiquidFormat).
 *
 * Failure policy. Fatal for the part (WorldParseException, file + chunk + offset): the chunk walk; a missing or
 * short MVER; an MCNK count other than 256 (root, tex0); and the placement records an obj file exists for -- an
 * MDDF/MODF/MLDD that is repeated, not a whole number of records, or holds a non-finite position, rotation or
 * extent. Everything else is non-fatal: a malformed MCNK, MH2O, MLDX, MDID/MHID, a repeated optional chunk (MH2O,
 * MAMP, MTXP, MDID, MHID, MLDX: the first copy is used) is marked Malformed with a WorldIssue and the rest of the
 * part is returned; inside a usable MH2O, a bad attribute block, bitmap or vertex block costs only that chunk's
 * attributes or that instance. Never-seen but well-formed data (a non-empty MCSE, flag 0x10000 clear, mixed alpha
 * bit depths, a liquid height outside its instance's min/max) is kept and reported as Unexpected.
 *
 * C++ notes: `what` names the file in messages; empty = the C# default ("adt root X_Y", ...).
 */

#ifndef _WMV_WORLD_ADTPARSER_H_
#define _WMV_WORLD_ADTPARSER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "AdtModel.h"
#include "TerrainLayout.h"
#include "WorldApi.h"
#include "WorldChunks.h"

namespace wmv
{
namespace world
{
class _WMVWORLD_API_ AdtParser
{
public:
  static constexpr int ChunksPerTile = 256;
  static constexpr int McnkHeaderSize = 128;
  static constexpr int HeightsBytes = TerrainVertexLayout::VertexCount * 4;       // 580
  static constexpr int NormalTripletBytes = TerrainVertexLayout::VertexCount * 3; // 435
  static constexpr int VertexColorBytes = TerrainVertexLayout::VertexCount * 4;   // 580
  static constexpr int AlphaTexels = 64 * 64;
  static constexpr int LiquidHeaderSize = 12;
  static constexpr int LiquidInstanceSize = 24;
  static constexpr int LiquidAttributesSize = 16;
  /// Largest |height| accepted for a liquid vertex (the terrain builder's bound).
  static constexpr float LiquidMaxAbsHeight = 65536.0f;
  /// The most layers a chunk may have here: 8, the most any audited chunk has (Retail map 2923; the stage 01 audit
  /// saw at most 5, Classic Beta); a larger count is rejected so a corrupt count cannot multiply the decoded maps.
  static constexpr int MaxLayers = 8;

  static AdtRoot ParseRoot(const std::vector<uint8_t>& data, int tileX, int tileY, const std::string& what = std::string());
  static AdtTex ParseTex(const std::vector<uint8_t>& data, const std::string& what = std::string());
  static AdtObj0 ParseObj0(const std::vector<uint8_t>& data, int tileX, int tileY, const std::string& what = std::string());
  static AdtObj1 ParseObj1(const std::vector<uint8_t>& data, int tileX, int tileY, const std::string& what = std::string());
  static AdtLod ParseLod(const std::vector<uint8_t>& data, const std::string& what = std::string());

  /// Decode every alpha map of one chunk (rules in the file header). False, with an issue, when the layer list
  /// and MCAL disagree; nothing is reinterpreted. `maps` gets one entry per layer (nullopt for layer 0 and for
  /// layers not decoded). `mclyOffset` and `mcnkOffset` (absolute) are where issues about the layers and the chunk
  /// are reported.
  static bool DecodeAlpha(const std::vector<uint8_t>& data, const std::optional<WorldChunk>& mcal,
                          const std::vector<TerrainLayer>& layers, const std::string& what, const std::string& label,
                          std::vector<WorldIssue>& issues, std::vector<std::optional<AlphaMap>>& maps,
                          long long mclyOffset = 0, long long mcnkOffset = 0);

  /// RLE from `start`, never reading at or past `end`. Exactly 4096 values; a run that would cross the end of the
  /// map is an error (the audit found none). Bad arguments give nullopt, with the reason in `error`.
  static std::optional<AlphaMap> DecodeRle(const std::vector<uint8_t>& data, int start, int end, std::string& error);

  /// An uncompressed map occupying [off, next) of an MCAL at `mcalStart` of `mcalSize` bytes: 4096 bytes = 8-bit,
  /// 2048 = 4-bit (low nibble first); any other gap, or arguments outside the buffer, give nullopt with the reason.
  static std::optional<AlphaMap> DecodeRaw(const std::vector<uint8_t>& data, int mcalStart, int mcalSize, long long off,
                                           long long next, std::string& error);

  /// Parse whichever parts of a tile were supplied, each on its own. `maidWords` (the tile's eight MAID words, or
  /// null when unknown) tells "the WDT names no such file" apart from "named but not supplied". A null part
  /// pointer = not supplied. A malformed part is reported in its own result; it never changes another part's
  /// state.
  static AdtTile ParseTile(int tileX, int tileY, const std::vector<uint32_t>* maidWords, const std::vector<uint8_t>* root,
                           const std::vector<uint8_t>* tex0, const std::vector<uint8_t>* obj0,
                           const std::vector<uint8_t>* obj1, const std::vector<uint8_t>* lod);
};
} // namespace world
} // namespace wmv

#endif
