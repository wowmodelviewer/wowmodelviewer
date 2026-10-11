// AdtModel.cs
//
// Result types of the split-ADT parsers (AdtParser.cs). A terrain tile is up to five files named by
// MAID: root, tex0, obj0, obj1, lod (audit 5.1); each part is parsed and reported on its own, so a
// missing or malformed tex0 never makes the root look corrupt.
//
// Field evidence is cited per field below (section numbers of docs/world-viewer/00-audit.md).
// Fields whose meaning the audit did not establish are kept raw with a "Raw" suffix and are never
// interpreted. No UnityEngine types. Every array here is owned by the result object; no result
// keeps a reference to the caller's input buffer.

using System.Collections.Generic;

namespace Wmv.Wow.World
{
    // ===================================================================================== root

    /// <summary>One root MCNK (128-byte header + sub-chunks). Offsets are relative to the MCNK chunk.</summary>
    public sealed class AdtRootChunk
    {
        /// <summary>Position of this MCNK among the root's MCNKs (0..255).</summary>
        public int Index;
        public WorldDataStatus Status;            // Decoded, or Malformed (see the root's issues)

        // ---- header (5.2); offsets proven by what each field equals in all 33,280 audited chunks
        public byte[] RawHeader;                  // the full 128 bytes, for fields not named here
        public uint Flags;                        // +0: raw; 0x40 <=> MCCV present, 0x1 <=> tex0 MCSH present,
                                                  //     0x10000 = high-resolution hole mask (all 100% correlations)
        public uint IndexX;                       // +4: == Index % 16 in 100% of chunks
        public uint IndexY;                       // +8: == Index / 16
        public uint AreaId;                       // +52: an AreaTable id (resolved per client in the audit)
        public WorldFloat3 StoredPosition;        // +104: float32 as stored; x/y within 1/768 yd (0.0013021) of the
                                                  //       index formula (6.1) -- use TerrainCoordinates for x/y
        public TerrainHoles Holes;                // +20 (64-bit, flag 0x10000) / +60 (16-bit, not decoded)

        // ---- sub-chunks
        public float[] Heights;                   // MCVT, 145 floats relative to StoredPosition.C (z)
        public sbyte[] NormalsRaw;                // MCNR, first 435 bytes as stored (145 triplets); component
                                                  //   order/scale NOT established (audit U9) -- not decoded
        public byte[] NormalsTrailingRaw;         // MCNR bytes after the 435 (13 in every audited chunk)
        public byte[] VertexColorsRaw;            // MCCV, 145 x 4 bytes as stored, or null; channel order
                                                  //   NOT established -- not decoded
        public int McseSize = -1;                 // MCSE payload size (0 in every audited chunk), -1 = absent
        public WorldUnsupportedChunk[] Unsupported;  // MCBB, MPTX and anything else

        public bool HasVertexColors { get { return VertexColorsRaw != null; } }
    }

    /// <summary>MH2O per-chunk header (12 bytes): offsets from the start of MH2O's payload (proven:
    /// every instance they point at is in bounds in all audited tiles, 5.7).</summary>
    public struct LiquidChunkHeader
    {
        public uint InstancesOffset;
        public uint LayerCount;
        public uint AttributesOffset;             // located, not decoded
    }

    /// <summary>One 24-byte MH2O instance, kept raw. Its boundaries are proven; the names below are
    /// the audit's Python reader's and are NOT verified (U14): no liquid type, material, height or
    /// vertex meaning is asserted here.</summary>
    public struct LiquidInstanceRaw
    {
        public ushort Word0;      // Python: liquid_type (values 1, 2, 5, 7, 301 seen; not checked against DB2)
        public ushort Word2;      // Python: object_or_lvf (42 or larger seen)
        public float Float4;      // Python: min_height
        public float Float8;      // Python: max_height
        public byte Byte12;       // Python: x
        public byte Byte13;       // Python: y
        public byte Byte14;       // Python: width
        public byte Byte15;       // Python: height
        public uint Word16;       // Python: offset of the exists bitmap (not decoded)
        public uint Word20;       // Python: offset of the vertex data (not decoded)
    }

    public sealed class AdtLiquids
    {
        /// <summary>PartiallyDecoded when the header table and every instance record are in bounds;
        /// Malformed otherwise. Vertex data, bitmaps and attributes are never decoded.</summary>
        public WorldDataStatus Status;
        public int DataOffset;                    // MH2O payload start in the root file
        public int Size;
        public LiquidChunkHeader[] Headers;       // 256
        public LiquidInstanceRaw[][] Instances;   // per chunk
    }

    public sealed class AdtRoot
    {
        public string What;
        public int TileX, TileY;
        public uint Version;
        public WorldChunk[] Chunks;               // every top-level chunk, in file order
        public AdtRootChunk[] Mcnk;               // 256
        public AdtLiquids Liquids;                // null when the root has no MH2O
        public WorldUnsupportedChunk[] Unsupported;   // MHDR, MFBO, MBMH/MBBB/MBMI/MBNV, ...
        public List<WorldIssue> Issues = new List<WorldIssue>();
    }

    // ===================================================================================== tex0

    /// <summary>MCLY record (16 bytes, 5.4).</summary>
    public struct TerrainLayer
    {
        public uint TextureIndex;     // +0: index into MDID (every audited index is in range)
        public uint Flags;            // +4: raw; 0x100 on exactly the layers after the first (100%),
                                      //     0x200 = compressed alpha (proven by RLE ending at the next
                                      //     layer); other bits (0x1-0x80, 0x400) UNKNOWN (U10)
        public uint AlphaOffset;      // +8: byte offset into MCAL (proven: offsets tile MCAL exactly)
        public uint Word12Raw;        // +12: meaning not established (the audit draft's "effect id" is a name only)
    }

    public enum AlphaStorage
    {
        /// <summary>Flag 0x200: RLE, 4096 values of 0..255.</summary>
        Compressed,
        /// <summary>4096 bytes, one value of 0..255 per texel.</summary>
        Uncompressed8,
        /// <summary>2048 bytes, two texels per byte, low nibble first: values 0..15.</summary>
        Uncompressed4,
    }

    /// <summary>A decoded 64x64 alpha map, texel (row, col) = Samples[row*64 + col], rows along the
    /// vertex rows (5.4, 6.5). 4-bit maps keep their 0..15 samples: how the client expands them to
    /// 8 bits is UNKNOWN (audit U19), so no expansion is offered.</summary>
    public sealed class AlphaMap
    {
        public AlphaStorage Storage;
        public byte[] Samples;        // 4096
        public int StoredBytes;       // bytes this layer occupies in MCAL

        public int BitDepth { get { return Storage == AlphaStorage.Uncompressed4 ? 4 : 8; } }

        public byte Sample(int row, int col)
        {
            if (row < 0 || row > 63 || col < 0 || col > 63)
                throw new System.ArgumentOutOfRangeException("row/col", "alpha texel row/column must be 0..63");
            return Samples[row * 64 + col];
        }

        /// <summary>8-bit values for material preparation. True for 8-bit maps (compressed or not):
        /// `values` is this map's own Samples array, not a copy -- treat it as read-only and copy it
        /// before editing in place. False for 4-bit maps, with the unresolved requirement in `reason`
        /// -- never a guessed scale.</summary>
        public bool TryGetEightBit(out byte[] values, out string reason)
        {
            if (BitDepth == 8)
            {
                values = Samples;
                reason = null;
                return true;
            }
            values = null;
            reason = "4-bit alpha: the client's 4-bit to 8-bit expansion is not established (audit U19); " +
                     "Retail re-encodes these maps as ~16n+15, which is evidence of a conversion, not of the draw-time rule";
            return false;
        }
    }

    public sealed class AdtTexChunk
    {
        public int Index;
        public WorldDataStatus Status;            // Decoded, or Malformed (alpha not usable; see issues)
        public TerrainLayer[] Layers;             // MCLY (empty when absent)
        public AlphaMap[] Alpha;                  // per layer; null for layer 0 and for undecodable layers
        public byte[] ShadowRaw;                  // MCSH (512 bytes) or null; bit layout NOT established
        public int AlphaSize = -1;                // MCAL payload size, -1 = absent
        public WorldUnsupportedChunk[] Unsupported;
    }

    public sealed class AdtTex
    {
        public string What;
        public uint Version;
        public WorldChunk[] Chunks;
        public byte[] MampRaw;                    // MAMP payload; value meaning UNKNOWN (U12)
        public uint[] DiffuseTextureIds;          // MDID FileDataIDs
        public uint[] HeightTextureIds;           // MHID FileDataIDs (blending rule UNKNOWN, U11)
        public byte[] MtxpRaw;                    // MTXP payload (16 bytes per texture), fields UNKNOWN (U11)
        public AdtTexChunk[] Mcnk;                // 256
        public WorldUnsupportedChunk[] Unsupported;
        public List<WorldIssue> Issues = new List<WorldIssue>();
    }

    // ===================================================================================== obj0 / obj1 / lod

    public sealed class AdtObj0Chunk
    {
        public int Index;
        public uint[] McrdRaw;        // MCRD u32 list (counts measured only; meaning not established)
        public uint[] McrwRaw;        // MCRW u32 list (same)
        public WorldUnsupportedChunk[] Unsupported;
    }

    public sealed class AdtObj0
    {
        public string What;
        public uint Version;
        public WorldChunk[] Chunks;
        public DoodadPlacement[] Doodads;         // MDDF, every record as stored (no de-duplication)
        public WmoPlacement[] Wmos;               // MODF, every record as stored
        public AdtObj0Chunk[] Mcnk;               // 256
        public WorldUnsupportedChunk[] Unsupported;   // MWDR/MWDS, MLMB, MMDX/MMID/MWMO/MWID, ...
        public List<WorldIssue> Issues = new List<WorldIssue>();
    }

    public sealed class AdtObj1
    {
        public string What;
        public uint Version;
        public WorldChunk[] Chunks;
        public DoodadPlacement[] Doodads;         // MLDD: obj0's MDDF placements, matched by uniqueId (order may differ, 5.6)
        public DoodadWorldBox[] DoodadBoxes;      // MLDX: box i belongs to MLDD record i; null when inconsistent
        public WorldUnsupportedChunk[] Unsupported;   // MLFD, MLDL, MDLI, MDLD, MLDB, MLMD, MLMX, MLMB, MWDR, MWDS
        public List<WorldIssue> Issues = new List<WorldIssue>();
    }

    /// <summary>_lod.adt: no chunk's layout is established (U17): an inventory only.</summary>
    public sealed class AdtLod
    {
        public string What;
        public WorldChunk[] Chunks;
        public List<WorldIssue> Issues = new List<WorldIssue>();
    }

    // ===================================================================================== tile

    public enum AdtPartState
    {
        /// <summary>The WDT names no file for this part (MAID word 0).</summary>
        NotNamed,
        /// <summary>Named, but the caller supplied no bytes (not resolved, not local, or not read), or an
        /// empty buffer (Error then says so).</summary>
        NotSupplied,
        Parsed,
        /// <summary>Bytes supplied but the part is malformed; Error says why. Other parts are unaffected.</summary>
        Malformed,
    }

    public sealed class AdtPartResult<T> where T : class
    {
        public AdtPartState State;
        public T Value;
        public string Error;
    }

    /// <summary>A terrain tile assembled from whichever parts were supplied, each reported on its own.</summary>
    public sealed class AdtTile
    {
        public int TileX, TileY;
        public AdtPartResult<AdtRoot> Root;
        public AdtPartResult<AdtTex> Tex0;
        public AdtPartResult<AdtObj0> Obj0;
        public AdtPartResult<AdtObj1> Obj1;
        public AdtPartResult<AdtLod> Lod;
    }
}
