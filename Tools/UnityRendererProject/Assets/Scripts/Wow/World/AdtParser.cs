// AdtParser.cs
//
// Bounds-safe readers for the split ADT parts (root, tex0, obj0, obj1, lod), from the bytes the
// caller supplies. See AdtModel.cs for the result types and per-field evidence; the rules below
// are the ones docs/world-viewer/00-audit.md proved against the bytes of all four audited clients:
//   - Every part is a flat chunk list (MVER first). A root has 256 MCNKs, each a 128-byte header
//     then sub-chunks; tex0 and obj0 have 256 MCNKs whose sub-chunks start at their first byte;
//     MCNK n of every part is the same terrain chunk (5.1).
//   - In split files layers live only in tex0 (root n_layers and alpha/shadow offsets are 0).
//   - MCAL (5.4): the layers after the first carry alpha maps, laid out back to back from offset
//     0 in layer order. A layer with MCLY flag 0x200 is RLE-compressed: a count byte n; top bit
//     set = repeat the next byte (n & 0x7F) times, clear = copy the next (n & 0x7F) bytes; it ends
//     at 4096 values, exactly where the next layer starts. An uncompressed layer occupies the gap
//     to the next layer (or to the end of MCAL): 4096 bytes = 8-bit, 2048 = 4-bit, low nibble
//     first. The bit depth comes from that spacing, never from an MPHD flag.
//
// Failure policy. Fatal for the part (WorldParseException, file + chunk + offset): the chunk walk;
// a missing or short MVER; an MCNK count other than 256 (root, tex0); and the placement records an
// obj file exists for -- an MDDF/MODF/MLDD that is repeated, not a whole number of records, or holds
// a non-finite position, rotation or extent. Everything else is non-fatal: a malformed MCNK, MH2O,
// MLDX, MDID/MHID, a repeated optional chunk (MH2O, MAMP, MTXP, MDID, MHID, MLDX: the first copy is
// used) is marked Malformed with a WorldIssue and the rest of the part is returned. Never-seen but
// well-formed data (a non-empty MCSE, flag 0x10000 clear, mixed alpha bit depths) is kept and
// reported as Unexpected.

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    public static class AdtParser
    {
        public const int ChunksPerTile = 256;
        public const int McnkHeaderSize = 128;
        public const int HeightsBytes = TerrainVertexLayout.VertexCount * 4;   // 580
        public const int NormalTripletBytes = TerrainVertexLayout.VertexCount * 3;  // 435
        public const int VertexColorBytes = TerrainVertexLayout.VertexCount * 4;   // 580
        public const int AlphaTexels = 64 * 64;
        public const int LiquidHeaderSize = 12;
        public const int LiquidInstanceSize = 24;

        // ================================================================================== root

        public static AdtRoot ParseRoot(byte[] data, int tileX, int tileY, string what = null)
        {
            what = what ?? string.Format("adt root {0}_{1}", tileX, tileY);
            List<WorldChunk> top = WalkFile(data, what);
            var root = new AdtRoot { What = what, TileX = tileX, TileY = tileY, Chunks = top.ToArray() };
            root.Version = ReadVersion(data, top, what, root.Issues);
            List<WorldChunk> mcnk = RequireMcnk(top, what);
            var unsupported = new List<WorldUnsupportedChunk>();
            root.Mcnk = new AdtRootChunk[ChunksPerTile];
            for (int i = 0; i < ChunksPerTile; i++)
                root.Mcnk[i] = ParseRootChunk(data, mcnk[i], i, what, root.Issues);

            foreach (WorldChunk c in top)
            {
                switch (c.Tag)
                {
                    case "MVER": case "MCNK":
                        break;
                    case "MH2O":
                        if (root.Liquids != null)
                        {
                            root.Issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, "MH2O", c.HeaderOffset,
                                                           "appears more than once (never in the audited files); the first copy is used"));
                            break;
                        }
                        root.Liquids = ParseLiquids(data, c, what, root.Issues);
                        break;
                    case "MHDR":
                        unsupported.Add(Unsupported(c, "header offsets not audited; chunks are found by walking"));
                        break;
                    case "MFBO": case "MBMH": case "MBBB": case "MBMI": case "MBNV":
                        unsupported.Add(Unsupported(c, "layout not established (audit U18)"));
                        break;
                    default:
                        unsupported.Add(Unsupported(c, "not seen in the audited root files"));
                        root.Issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, WorldChunkWalker.Printable(c.Tag),
                                                       c.DataOffset, "chunk not seen in the audited root files; kept undecoded"));
                        break;
                }
            }
            root.Unsupported = unsupported.ToArray();
            return root;
        }

        static AdtRootChunk ParseRootChunk(byte[] data, WorldChunk c, int index, string what, List<WorldIssue> issues)
        {
            var ch = new AdtRootChunk { Index = index, Status = WorldDataStatus.Decoded };
            string label = "MCNK " + index;
            if (c.Size < McnkHeaderSize)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label, c.DataOffset, string.Format(
                    "{0} byte(s), smaller than its 128-byte header", c.Size)));
                ch.Status = WorldDataStatus.Malformed;
                return ch;
            }
            var h = new WorldSpan(data, c.DataOffset, McnkHeaderSize, what, label);
            ch.RawHeader = h.Bytes(0, McnkHeaderSize);
            ch.Flags = h.U32(0);
            ch.IndexX = h.U32(4);
            ch.IndexY = h.U32(8);
            ch.AreaId = h.U32(52);
            try
            {
                ch.StoredPosition = new WorldFloat3(h.F32(104), h.F32(108), h.F32(112));
            }
            catch (WorldParseException e)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label, e.Offset, "stored position: " + e.Reason));
                ch.Status = WorldDataStatus.Malformed;
            }
            if (ch.IndexX != (uint)(index % 16) || ch.IndexY != (uint)(index / 16))
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label, c.DataOffset + 4, string.Format(
                    "header index ({0}, {1}) is not the chunk's position ({2}, {3}); every audited chunk has them equal",
                    ch.IndexX, ch.IndexY, index % 16, index / 16)));
                ch.Status = WorldDataStatus.Malformed;
            }
            if ((ch.Flags & 0x10000) != 0)
            {
                ch.Holes = new TerrainHoles { Kind = HoleMaskKind.HighResolution, HighResolutionBits = h.U64(20), LowResolutionRaw = h.U16(60) };
                if (ch.Holes.LowResolutionRaw != 0)
                    issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, label, c.DataOffset + 60,
                                              "the 16-bit hole field is non-zero although flag 0x10000 is set (always 0 in the audit)"));
            }
            else
            {
                ch.Holes = new TerrainHoles { Kind = HoleMaskKind.LowResolutionUnsupported, LowResolutionRaw = h.U16(60) };
                issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, label, c.DataOffset,
                                          "flag 0x10000 clear (set on every audited chunk): bytes 20..27 are not read as holes"));
                if (ch.Holes.LowResolutionRaw != 0)
                    issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, label, c.DataOffset + 60,
                                              "low-resolution hole mask: its bit layout is not established; not decoded"));
            }

            List<WorldChunk> sub;
            try
            {
                sub = WorldChunkWalker.Walk(data, c.DataOffset + McnkHeaderSize, c.DataOffset + c.Size, what, label);
            }
            catch (WorldParseException e)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, e.Chunk, e.Offset, e.Reason));
                ch.Status = WorldDataStatus.Malformed;
                return ch;
            }

            var unsupported = new List<WorldUnsupportedChunk>();
            bool haveHeights = false;
            foreach (WorldChunk s in sub)
            {
                string slabel = label + " " + WorldChunkWalker.Printable(s.Tag);
                switch (s.Tag)
                {
                    case "MCVT":
                        if (haveHeights || s.Size != HeightsBytes)
                        {
                            issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, slabel, s.DataOffset, haveHeights
                                ? "appears more than once" : string.Format("{0} byte(s), expected 580 (145 floats)", s.Size)));
                            ch.Status = WorldDataStatus.Malformed;
                            break;
                        }
                        try
                        {
                            var hs = new WorldSpan(data, s, what);
                            var heights = new float[TerrainVertexLayout.VertexCount];
                            for (int k = 0; k < heights.Length; k++)
                                heights[k] = hs.F32(4 * k);
                            ch.Heights = heights;
                            haveHeights = true;
                        }
                        catch (WorldParseException e)
                        {
                            issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, slabel, e.Offset, e.Reason));
                            ch.Status = WorldDataStatus.Malformed;
                        }
                        break;
                    case "MCNR":
                        if (ch.NormalsRaw != null || s.Size < NormalTripletBytes)
                        {
                            issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, slabel, s.DataOffset, ch.NormalsRaw != null
                                ? "appears more than once" : string.Format("{0} byte(s), fewer than 435 (145 triplets)", s.Size)));
                            ch.Status = WorldDataStatus.Malformed;
                            break;
                        }
                        {
                            var raw = new sbyte[NormalTripletBytes];
                            Buffer.BlockCopy(data, s.DataOffset, raw, 0, NormalTripletBytes);
                            ch.NormalsRaw = raw;
                            ch.NormalsTrailingRaw = new WorldSpan(data, s, what).Bytes(NormalTripletBytes, s.Size - NormalTripletBytes);
                        }
                        break;
                    case "MCCV":
                        if (ch.VertexColorsRaw != null || s.Size != VertexColorBytes)
                        {
                            issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, slabel, s.DataOffset, ch.VertexColorsRaw != null
                                ? "appears more than once" : string.Format("{0} byte(s), expected 580 (145 x 4)", s.Size)));
                            ch.Status = WorldDataStatus.Malformed;
                            break;
                        }
                        ch.VertexColorsRaw = new WorldSpan(data, s, what).Bytes(0, s.Size);
                        break;
                    case "MCSE":
                        if (ch.McseSize >= 0)
                        {
                            unsupported.Add(Unsupported(s, "repeated MCSE"));
                            issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, slabel, s.DataOffset,
                                                      "appears more than once (never in the audited files); the first copy is used"));
                            break;
                        }
                        ch.McseSize = s.Size;
                        if (s.Size != 0)
                        {
                            unsupported.Add(Unsupported(s, "non-empty MCSE: layout not established"));
                            issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, slabel, s.DataOffset,
                                                      s.Size + " byte(s) of MCSE (0 in every audited chunk); kept undecoded"));
                        }
                        break;
                    case "MCBB": case "MPTX":
                        unsupported.Add(Unsupported(s, "layout not established (audit U18)"));
                        break;
                    default:
                        unsupported.Add(Unsupported(s, "not seen in audited root MCNKs"));
                        issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, slabel, s.DataOffset,
                                                  "sub-chunk not seen in audited root MCNKs; kept undecoded"));
                        break;
                }
            }
            ch.Unsupported = unsupported.ToArray();
            if (!haveHeights && ch.Status == WorldDataStatus.Decoded)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label, c.DataOffset, "no MCVT (every audited root MCNK has one)"));
                ch.Status = WorldDataStatus.Malformed;
            }
            if (ch.NormalsRaw == null)
                issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, label, c.DataOffset, "no usable MCNR (every audited root MCNK has one)"));
            if (((ch.Flags & 0x40) != 0) != ch.HasVertexColors)
                issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, label, c.DataOffset, string.Format(
                    "flag 0x40 is {0} but MCCV is {1} (they agree in every audited chunk)",
                    (ch.Flags & 0x40) != 0 ? "set" : "clear", ch.HasVertexColors ? "present" : "absent")));
            return ch;
        }

        static AdtLiquids ParseLiquids(byte[] data, WorldChunk c, string what, List<WorldIssue> issues)
        {
            var liq = new AdtLiquids { DataOffset = c.DataOffset, Size = c.Size, Status = WorldDataStatus.PartiallyDecoded };
            var s = new WorldSpan(data, c, what);
            if (s.Size < ChunksPerTile * LiquidHeaderSize)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, "MH2O", c.DataOffset, string.Format(
                    "{0} byte(s), fewer than the 256 x 12-byte header table", s.Size)));
                liq.Status = WorldDataStatus.Malformed;
                return liq;
            }
            liq.Headers = new LiquidChunkHeader[ChunksPerTile];
            liq.Instances = new LiquidInstanceRaw[ChunksPerTile][];
            // Validate every range before allocating anything: in all audited tiles the instance tables
            // lie after the 3,072-byte header table and never overlap, so the instances can never
            // outnumber the bytes that hold them (an overlapping table could otherwise multiply them).
            const int table = ChunksPerTile * LiquidHeaderSize;
            var ranges = new List<KeyValuePair<long, long>>();
            for (int n = 0; n < ChunksPerTile; n++)
            {
                var hd = new LiquidChunkHeader
                {
                    InstancesOffset = s.U32(LiquidHeaderSize * n),
                    LayerCount = s.U32(LiquidHeaderSize * n + 4),
                    AttributesOffset = s.U32(LiquidHeaderSize * n + 8),
                };
                liq.Headers[n] = hd;
                if (hd.LayerCount == 0)
                    continue;
                long end = (long)hd.InstancesOffset + (long)hd.LayerCount * LiquidInstanceSize;
                if (hd.InstancesOffset < table || end > s.Size)
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, "MH2O", s.AbsoluteOffset(LiquidHeaderSize * n), string.Format(
                        "chunk {0}: {1} instance(s) at +0x{2:X} are not inside the {3}-byte chunk after its header table",
                        n, hd.LayerCount, hd.InstancesOffset, s.Size)));
                    liq.Status = WorldDataStatus.Malformed;
                }
                else
                    ranges.Add(new KeyValuePair<long, long>(hd.InstancesOffset, end));
            }
            ranges.Sort((a, b) => a.Key.CompareTo(b.Key));
            for (int i = 1; i < ranges.Count; i++)
                if (ranges[i].Key < ranges[i - 1].Value)
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, "MH2O", s.AbsoluteOffset((int)ranges[i].Key), string.Format(
                        "instance tables overlap (+0x{0:X} starts inside [+0x{1:X}, +0x{2:X})); no audited tile has overlapping tables",
                        ranges[i].Key, ranges[i - 1].Key, ranges[i - 1].Value)));
                    liq.Status = WorldDataStatus.Malformed;
                    break;
                }
            for (int n = 0; n < ChunksPerTile; n++)
            {
                LiquidChunkHeader hd = liq.Headers[n];
                if (liq.Status == WorldDataStatus.Malformed || hd.LayerCount == 0)
                {
                    liq.Instances[n] = new LiquidInstanceRaw[0];
                    continue;
                }
                var inst = new LiquidInstanceRaw[hd.LayerCount];
                for (int k = 0; k < inst.Length; k++)
                {
                    int o = (int)hd.InstancesOffset + LiquidInstanceSize * k;
                    inst[k] = new LiquidInstanceRaw
                    {
                        Word0 = s.U16(o), Word2 = s.U16(o + 2), Float4 = s.F32Raw(o + 4), Float8 = s.F32Raw(o + 8),
                        Byte12 = s.U8(o + 12), Byte13 = s.U8(o + 13), Byte14 = s.U8(o + 14), Byte15 = s.U8(o + 15),
                        Word16 = s.U32(o + 16), Word20 = s.U32(o + 20),
                    };
                }
                liq.Instances[n] = inst;
            }
            return liq;
        }

        // ================================================================================== tex0

        public static AdtTex ParseTex(byte[] data, string what = null)
        {
            what = what ?? "adt tex0";
            List<WorldChunk> top = WalkFile(data, what);
            var tex = new AdtTex { What = what, Chunks = top.ToArray() };
            tex.Version = ReadVersion(data, top, what, tex.Issues);
            List<WorldChunk> mcnk = RequireMcnk(top, what);
            var unsupported = new List<WorldUnsupportedChunk>();
            tex.DiffuseTextureIds = OptionalU32s(data, top, "MDID", what, tex.Issues);
            tex.HeightTextureIds = OptionalU32s(data, top, "MHID", what, tex.Issues);
            WorldChunk c;
            if (WorldChunkWalker.FindFirst(top, "MAMP", what, tex.Issues, out c))
                tex.MampRaw = new WorldSpan(data, c, what).Bytes(0, c.Size);
            if (WorldChunkWalker.FindFirst(top, "MTXP", what, tex.Issues, out c))
                tex.MtxpRaw = new WorldSpan(data, c, what).Bytes(0, c.Size);
            foreach (WorldChunk k in top)
            {
                switch (k.Tag)
                {
                    case "MVER": case "MCNK": case "MDID": case "MHID": case "MAMP": case "MTXP":
                        break;
                    case "MTEX":
                        unsupported.Add(Unsupported(k, "texture name table: absent from every audited client (FileDataIDs are in MDID)"));
                        tex.Issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, "MTEX", k.DataOffset,
                                                      "name-based texture table; MCLY indices are only resolved through MDID"));
                        break;
                    default:
                        unsupported.Add(Unsupported(k, "not seen in the audited tex0 files"));
                        tex.Issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, WorldChunkWalker.Printable(k.Tag), k.DataOffset,
                                                      "chunk not seen in the audited tex0 files; kept undecoded"));
                        break;
                }
            }
            tex.Unsupported = unsupported.ToArray();
            tex.Mcnk = new AdtTexChunk[ChunksPerTile];
            for (int i = 0; i < ChunksPerTile; i++)
                tex.Mcnk[i] = ParseTexChunk(data, mcnk[i], i, tex.DiffuseTextureIds.Length, what, tex.Issues);
            return tex;
        }

        /// <summary>An optional u32 FileDataID table (MDID, MHID): the first copy; a size that is not a
        /// whole number of u32 is Malformed and gives an empty table (layers then cannot resolve).</summary>
        static uint[] OptionalU32s(byte[] data, List<WorldChunk> top, string tag, string what, List<WorldIssue> issues)
        {
            WorldChunk c;
            if (!WorldChunkWalker.FindFirst(top, tag, what, issues, out c))
                return new uint[0];
            if (c.Size % 4 != 0)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, tag, c.DataOffset, string.Format(
                    "{0} byte(s) is not a whole number of u32 FileDataIDs; the table is not used", c.Size)));
                return new uint[0];
            }
            return new WorldSpan(data, c, what).U32Array();
        }

        /// <summary>The most layers a chunk may have here. The audited chunks have at most 5 (Classic
        /// Beta); a much larger count is rejected so a corrupt count cannot multiply the decoded maps.</summary>
        public const int MaxLayers = 8;

        static AdtTexChunk ParseTexChunk(byte[] data, WorldChunk c, int index, int textureCount, string what, List<WorldIssue> issues)
        {
            var ch = new AdtTexChunk { Index = index, Status = WorldDataStatus.Decoded, Layers = new TerrainLayer[0] };
            string label = "tex0 MCNK " + index;
            List<WorldChunk> sub;
            try
            {
                sub = WorldChunkWalker.Walk(data, c.DataOffset, c.DataOffset + c.Size, what, label);
            }
            catch (WorldParseException e)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, e.Chunk, e.Offset, e.Reason));
                ch.Status = WorldDataStatus.Malformed;
                return ch;
            }
            var unsupported = new List<WorldUnsupportedChunk>();
            WorldChunk mcal = default(WorldChunk), mcly = default(WorldChunk);
            bool haveMcal = false, haveMcly = false;
            foreach (WorldChunk s in sub)
            {
                string slabel = label + " " + WorldChunkWalker.Printable(s.Tag);
                switch (s.Tag)
                {
                    case "MCLY":
                        if (haveMcly || s.Size % 16 != 0 || s.Size / 16 > MaxLayers)
                        {
                            issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, slabel, s.DataOffset, haveMcly ? "appears more than once"
                                : s.Size % 16 != 0 ? string.Format("{0} byte(s) is not a whole number of 16-byte layers", s.Size)
                                : string.Format("{0} layers (at most 5 in the audited chunks; more than {1} are rejected)", s.Size / 16, MaxLayers)));
                            ch.Status = WorldDataStatus.Malformed;
                            break;
                        }
                        haveMcly = true;
                        mcly = s;
                        {
                            var ls = new WorldSpan(data, s, what);
                            var layers = new TerrainLayer[s.Size / 16];
                            for (int k = 0; k < layers.Length; k++)
                                layers[k] = new TerrainLayer { TextureIndex = ls.U32(16 * k), Flags = ls.U32(16 * k + 4),
                                                               AlphaOffset = ls.U32(16 * k + 8), Word12Raw = ls.U32(16 * k + 12) };
                            ch.Layers = layers;
                        }
                        break;
                    case "MCAL":
                        if (haveMcal)
                        {
                            issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, slabel, s.DataOffset, "appears more than once"));
                            ch.Status = WorldDataStatus.Malformed;
                            break;
                        }
                        mcal = s;
                        haveMcal = true;
                        ch.AlphaSize = s.Size;
                        break;
                    case "MCSH":
                        if (ch.ShadowRaw != null)
                        {
                            issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, slabel, s.DataOffset, "appears more than once"));
                            break;
                        }
                        ch.ShadowRaw = new WorldSpan(data, s, what).Bytes(0, s.Size);
                        if (s.Size != 512)
                            issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, slabel, s.DataOffset,
                                                      s.Size + " byte(s) (512 in every audited chunk)"));
                        break;
                    default:
                        unsupported.Add(Unsupported(s, "not seen in audited tex0 MCNKs"));
                        issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, slabel, s.DataOffset,
                                                  "sub-chunk not seen in audited tex0 MCNKs; kept undecoded"));
                        break;
                }
            }
            ch.Unsupported = unsupported.ToArray();
            for (int k = 0; k < ch.Layers.Length; k++)
                if (ch.Layers[k].TextureIndex >= (uint)textureCount)
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label + " MCLY", mcly.DataOffset + 16 * k, string.Format(
                        "layer {0} names texture {1}, but MDID has {2}", k, ch.Layers[k].TextureIndex, textureCount)));
                    ch.Status = WorldDataStatus.Malformed;
                }
            if (ch.Status == WorldDataStatus.Decoded)
            {
                AlphaMap[] maps;
                if (DecodeAlpha(data, haveMcal ? (WorldChunk?)mcal : null, ch.Layers, what, label, issues, out maps,
                                haveMcly ? mcly.DataOffset : c.DataOffset, c.DataOffset))
                    ch.Alpha = maps;
                else
                    ch.Status = WorldDataStatus.Malformed;
            }
            return ch;
        }

        /// <summary>Decode every alpha map of one chunk (rules in the file header). False, with an
        /// issue, when the layer list and MCAL disagree; nothing is reinterpreted. `mclyOffset` and
        /// `mcnkOffset` (absolute) are where issues about the layers and the chunk are reported.</summary>
        public static bool DecodeAlpha(byte[] data, WorldChunk? mcal, TerrainLayer[] layers, string what, string label,
                                       List<WorldIssue> issues, out AlphaMap[] maps, long mclyOffset = 0, long mcnkOffset = 0)
        {
            maps = new AlphaMap[layers == null ? 0 : layers.Length];
            if (data == null || layers == null || issues == null)
                return false;
            if (mcal.HasValue)
            {
                WorldChunk mc = mcal.Value;
                if (mc.DataOffset < 0 || mc.Size < 0 || mc.DataOffset > data.Length - mc.Size)
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label + " MCAL", mc.DataOffset, "MCAL is outside the buffer"));
                    return false;
                }
            }
            for (int k = 0; k < layers.Length; k++)
            {
                bool has = (layers[k].Flags & 0x100) != 0;
                if (has != (k >= 1))
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, label + " MCLY", mclyOffset + 16 * k + 4,
                        string.Format("layer {0} has flag 0x100 {1}; in every audited chunk exactly the layers after the first have it. " +
                                      "Alpha not decoded", k, has ? "set" : "clear")));
                    return false;
                }
            }
            if (layers.Length <= 1)
            {
                if (mcal.HasValue && mcal.Value.Size > 0)
                    issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, label + " MCAL", mcal.Value.DataOffset,
                                              mcal.Value.Size + " byte(s) of MCAL but no layer after the first"));
                return true;
            }
            if (!mcal.HasValue)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label, mcnkOffset, string.Format(
                    "{0} layers but no MCAL", layers.Length)));
                return false;
            }
            WorldChunk m = mcal.Value;
            long running = 0;
            bool four = false, eight = false;
            for (int k = 1; k < layers.Length; k++)
            {
                uint off = layers[k].AlphaOffset;
                if (off != running)
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label + " MCAL", m.DataOffset + Math.Min(off, (uint)m.Size), string.Format(
                        "layer {0}'s alpha starts at +{1}, expected +{2} (maps are back to back from 0; layer {3} ended there)",
                        k, off, running, k - 1)));
                    return false;
                }
                AlphaMap map;
                string error;
                if ((layers[k].Flags & 0x200) != 0)
                    map = DecodeRle(data, m.DataOffset + (int)off, m.DataOffset + m.Size, out error);
                else
                {
                    long next = k + 1 < layers.Length ? layers[k + 1].AlphaOffset : (long)m.Size;
                    map = DecodeRaw(data, m.DataOffset, m.Size, off, next, out error);
                }
                if (map == null)
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label + " MCAL", m.DataOffset + off,
                                              "layer " + k + ": " + error));
                    return false;
                }
                maps[k] = map;
                running += map.StoredBytes;
                if (map.BitDepth == 4) four = true; else eight = true;
            }
            if (running != m.Size)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label + " MCAL", m.DataOffset + Math.Min(running, (long)m.Size), string.Format(
                    "the alpha maps use {0} of MCAL's {1} byte(s)", running, m.Size)));
                return false;
            }
            if (four && eight)
                issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, label + " MCAL", m.DataOffset,
                                          "4-bit and 8-bit (or compressed) maps in one chunk: no audited chunk mixes them; each map is decoded by its own spacing"));
            return true;
        }

        /// <summary>RLE from `start`, never reading at or past `end`. Exactly 4096 values; a run that
        /// would cross the end of the map is an error (the audit found none). Bad arguments give null.</summary>
        public static AlphaMap DecodeRle(byte[] data, int start, int end, out string error)
        {
            error = null;
            if (data == null || start < 0 || end > data.Length || start > end)
            {
                error = "compressed alpha range is outside the buffer";
                return null;
            }
            var v = new byte[AlphaTexels];
            int filled = 0, p = start;
            while (filled < AlphaTexels)
            {
                if (p >= end)
                {
                    error = string.Format("compressed alpha ends after {0} of 4096 values", filled);
                    return null;
                }
                int n = data[p++];
                int count = n & 0x7F;
                if (count > AlphaTexels - filled)
                {
                    error = string.Format("a run of {0} at value {1} crosses the end of the 4096-value map", count, filled);
                    return null;
                }
                if ((n & 0x80) != 0)
                {
                    if (p >= end)
                    {
                        error = "a fill run has no value byte";
                        return null;
                    }
                    byte b = data[p++];
                    for (int k = 0; k < count; k++)
                        v[filled++] = b;
                }
                else
                {
                    if (count > end - p)
                    {
                        error = string.Format("a copy run of {0} byte(s) passes the end of MCAL", count);
                        return null;
                    }
                    Buffer.BlockCopy(data, p, v, filled, count);
                    p += count;
                    filled += count;
                }
            }
            return new AlphaMap { Storage = AlphaStorage.Compressed, Samples = v, StoredBytes = p - start };
        }

        /// <summary>An uncompressed map occupying [off, next) of an MCAL at `mcalStart` of `mcalSize`
        /// bytes: 4096 bytes = 8-bit, 2048 = 4-bit (low nibble first); any other gap, or arguments
        /// outside the buffer, give null with the reason.</summary>
        public static AlphaMap DecodeRaw(byte[] data, int mcalStart, int mcalSize, long off, long next, out string error)
        {
            error = null;
            if (data == null || mcalStart < 0 || mcalSize < 0 || mcalStart > data.Length - mcalSize)
            {
                error = string.Format("MCAL at {0} of {1} byte(s) is outside the {2}-byte buffer", mcalStart, mcalSize, data == null ? 0 : data.Length);
                return null;
            }
            if (off < 0 || next < off || next > mcalSize)
            {
                error = string.Format("uncompressed alpha [{0}, {1}) is outside the {2}-byte MCAL", off, next, mcalSize);
                return null;
            }
            long len = next - off;
            var v = new byte[AlphaTexels];
            int p = mcalStart + (int)off;
            if (len == AlphaTexels)
            {
                Buffer.BlockCopy(data, p, v, 0, AlphaTexels);
                return new AlphaMap { Storage = AlphaStorage.Uncompressed8, Samples = v, StoredBytes = AlphaTexels };
            }
            if (len == AlphaTexels / 2)
            {
                for (int i = 0; i < AlphaTexels / 2; i++)
                {
                    byte b = data[p + i];
                    v[2 * i] = (byte)(b & 0x0F);
                    v[2 * i + 1] = (byte)(b >> 4);
                }
                return new AlphaMap { Storage = AlphaStorage.Uncompressed4, Samples = v, StoredBytes = AlphaTexels / 2 };
            }
            error = string.Format("uncompressed alpha of {0} byte(s): only 4096 (8-bit) and 2048 (4-bit) are established", len);
            return null;
        }

        // ================================================================================== obj0 / obj1 / lod

        public static AdtObj0 ParseObj0(byte[] data, int tileX, int tileY, string what = null)
        {
            what = what ?? string.Format("adt obj0 {0}_{1}", tileX, tileY);
            List<WorldChunk> top = WalkFile(data, what);
            var obj = new AdtObj0 { What = what, Chunks = top.ToArray() };
            obj.Version = ReadVersion(data, top, what, obj.Issues);
            WorldChunk mddf, modf;
            bool haveMddf = WorldChunkWalker.FindSingle(top, "MDDF", what, out mddf);
            obj.Doodads = haveMddf ? PlacementRecords.ReadDoodads(new WorldSpan(data, mddf, what), tileX, tileY, "obj0 MDDF") : new DoodadPlacement[0];
            bool haveModf = WorldChunkWalker.FindSingle(top, "MODF", what, out modf);
            obj.Wmos = haveModf ? PlacementRecords.ReadWmos(new WorldSpan(data, modf, what), tileX, tileY, "obj0 MODF") : new WmoPlacement[0];
            bool nameTables = WorldChunkWalker.Count(top, "MMID") + WorldChunkWalker.Count(top, "MWID") > 0;
            ReportUnflagged(obj.Doodads.Length, i => obj.Doodads[i].NameKind == PlacementNameKind.NameTableEntry, "MDDF", "0x40",
                            haveMddf ? mddf.DataOffset : 0, PlacementRecords.DoodadRecordSize, nameTables, what, obj.Issues);
            ReportUnflagged(obj.Wmos.Length, i => obj.Wmos[i].NameKind == PlacementNameKind.NameTableEntry, "MODF", "0x8",
                            haveModf ? modf.DataOffset : 0, PlacementRecords.WmoRecordSize, nameTables, what, obj.Issues);
            var unsupported = new List<WorldUnsupportedChunk>();
            var mcnk = new List<AdtObj0Chunk>();
            foreach (WorldChunk k in top)
            {
                switch (k.Tag)
                {
                    case "MVER": case "MDDF": case "MODF":
                        break;
                    case "MCNK":
                        mcnk.Add(ParseObj0Chunk(data, k, mcnk.Count, what, obj.Issues));
                        break;
                    case "MWDR": case "MWDS": case "MLMB":
                        unsupported.Add(Unsupported(k, "layout not established (audit U18)"));
                        break;
                    case "MMDX": case "MMID": case "MWMO": case "MWID":
                        unsupported.Add(Unsupported(k, "name table: absent from every audited client"));
                        obj.Issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, k.Tag, k.DataOffset,
                                                      "name table; placements without a FileDataID flag cannot be resolved here"));
                        break;
                    default:
                        unsupported.Add(Unsupported(k, "not seen in the audited obj0 files"));
                        obj.Issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, WorldChunkWalker.Printable(k.Tag), k.DataOffset,
                                                      "chunk not seen in the audited obj0 files; kept undecoded"));
                        break;
                }
            }
            if (mcnk.Count != ChunksPerTile)
            {
                int first = -1;
                foreach (WorldChunk k in top)
                    if (k.Tag == "MCNK") { first = k.HeaderOffset; break; }
                obj.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MCNK", first >= 0 ? first : data.Length, string.Format(
                    "{0} MCNK(s), 256 in every audited obj0", mcnk.Count)));
            }
            obj.Mcnk = mcnk.ToArray();
            obj.Unsupported = unsupported.ToArray();
            return obj;
        }

        /// <summary>One issue per chunk when records lack the FileDataID flag: every audited record has
        /// it, and without a name table such a name cannot be resolved.</summary>
        static void ReportUnflagged(int count, Func<int, bool> unflagged, string tag, string flag, long dataOffset, int recordSize,
                                    bool nameTables, string what, List<WorldIssue> issues)
        {
            int n = 0, first = -1;
            for (int i = 0; i < count; i++)
                if (unflagged(i)) { n++; if (first < 0) first = i; }
            if (n > 0)
                issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, tag, dataOffset + (long)recordSize * first, string.Format(
                    "{0} record(s) without the FileDataID flag {1} (first: record {2}); every audited record has it{3}",
                    n, flag, first, nameTables ? "; the file's name table is not decoded" : ", and the file has no name table to resolve them")));
        }

        static AdtObj0Chunk ParseObj0Chunk(byte[] data, WorldChunk c, int index, string what, List<WorldIssue> issues)
        {
            var ch = new AdtObj0Chunk { Index = index };
            string label = "obj0 MCNK " + index;
            List<WorldChunk> sub;
            try
            {
                sub = WorldChunkWalker.Walk(data, c.DataOffset, c.DataOffset + c.Size, what, label);
            }
            catch (WorldParseException e)
            {
                issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, e.Chunk, e.Offset, e.Reason));
                return ch;
            }
            var unsupported = new List<WorldUnsupportedChunk>();
            foreach (WorldChunk s in sub)
            {
                try
                {
                    if (s.Tag == "MCRD" && ch.McrdRaw == null)
                        ch.McrdRaw = new WorldSpan(data, s, what).U32Array();
                    else if (s.Tag == "MCRW" && ch.McrwRaw == null)
                        ch.McrwRaw = new WorldSpan(data, s, what).U32Array();
                    else
                    {
                        unsupported.Add(Unsupported(s, "not seen (or repeated) in audited obj0 MCNKs"));
                        issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, label + " " + WorldChunkWalker.Printable(s.Tag),
                                                  s.DataOffset, "sub-chunk not seen (or repeated) in audited obj0 MCNKs; kept undecoded"));
                    }
                }
                catch (WorldParseException e)
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, label + " " + e.Chunk, e.Offset, e.Reason));
                }
            }
            ch.Unsupported = unsupported.ToArray();
            return ch;
        }

        public static AdtObj1 ParseObj1(byte[] data, int tileX, int tileY, string what = null)
        {
            what = what ?? string.Format("adt obj1 {0}_{1}", tileX, tileY);
            List<WorldChunk> top = WalkFile(data, what);
            var obj = new AdtObj1 { What = what, Chunks = top.ToArray() };
            obj.Version = ReadVersion(data, top, what, obj.Issues);
            WorldChunk c;
            bool haveMldd = WorldChunkWalker.FindSingle(top, "MLDD", what, out c);
            obj.Doodads = haveMldd ? PlacementRecords.ReadDoodads(new WorldSpan(data, c, what), tileX, tileY, "obj1 MLDD") : new DoodadPlacement[0];
            ReportUnflagged(obj.Doodads.Length, i => obj.Doodads[i].NameKind == PlacementNameKind.NameTableEntry, "MLDD", "0x40",
                            haveMldd ? c.DataOffset : 0, PlacementRecords.DoodadRecordSize, false, what, obj.Issues);
            if (WorldChunkWalker.FindFirst(top, "MLDX", what, obj.Issues, out c))
            {
                var s = new WorldSpan(data, c, what);
                if (s.Size != PlacementRecords.DoodadBoxRecordSize * obj.Doodads.Length)
                    obj.Issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, "MLDX", c.DataOffset, string.Format(
                        "{0} byte(s), expected 28 per MLDD record ({1})", s.Size, obj.Doodads.Length)));
                else
                {
                    try
                    {
                        obj.DoodadBoxes = PlacementRecords.ReadDoodadBoxes(s);
                    }
                    catch (WorldParseException e)
                    {
                        obj.Issues.Add(new WorldIssue(WorldIssueKind.Malformed, what, "MLDX", e.Offset, e.Reason + "; the boxes are not used"));
                    }
                }
            }
            var unsupported = new List<WorldUnsupportedChunk>();
            foreach (WorldChunk k in top)
            {
                switch (k.Tag)
                {
                    case "MVER": case "MLDD": case "MLDX":
                        break;
                    case "MLFD": case "MLDL": case "MDLI": case "MDLD": case "MLDB": case "MLMD": case "MLMX": case "MLMB":
                    case "MWDR": case "MWDS":
                        unsupported.Add(Unsupported(k, "layout not established (audit U17/U18)"));
                        break;
                    default:
                        unsupported.Add(Unsupported(k, "not seen in the audited obj1 files"));
                        obj.Issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, WorldChunkWalker.Printable(k.Tag), k.DataOffset,
                                                      "chunk not seen in the audited obj1 files; kept undecoded"));
                        break;
                }
            }
            obj.Unsupported = unsupported.ToArray();
            return obj;
        }

        public static AdtLod ParseLod(byte[] data, string what = null)
        {
            what = what ?? "adt lod";
            List<WorldChunk> top = WalkFile(data, what);
            var lod = new AdtLod { What = what, Chunks = top.ToArray() };
            ReadVersion(data, top, what, lod.Issues);
            lod.Issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, "lod", 0,
                                          "_lod.adt chunk layouts are not established (audit U17); chunk inventory only"));
            return lod;
        }

        // ================================================================================== tile

        /// <summary>
        /// Parse whichever parts of a tile were supplied, each on its own. `maidWords` (the tile's
        /// eight MAID words, or null when unknown) tells "the WDT names no such file" apart from
        /// "named but not supplied". A malformed part is reported in its own result; it never
        /// changes another part's state.
        /// </summary>
        public static AdtTile ParseTile(int tileX, int tileY, uint[] maidWords, byte[] root, byte[] tex0, byte[] obj0,
                                        byte[] obj1, byte[] lod)
        {
            var t = new AdtTile { TileX = tileX, TileY = tileY };
            string tile = tileX + "_" + tileY;
            t.Root = Part(maidWords, WdtTileFileRole.RootAdt, root, () => ParseRoot(root, tileX, tileY, "adt root " + tile));
            t.Tex0 = Part(maidWords, WdtTileFileRole.Tex0Adt, tex0, () => ParseTex(tex0, "adt tex0 " + tile));
            t.Obj0 = Part(maidWords, WdtTileFileRole.Obj0Adt, obj0, () => ParseObj0(obj0, tileX, tileY, "adt obj0 " + tile));
            t.Obj1 = Part(maidWords, WdtTileFileRole.Obj1Adt, obj1, () => ParseObj1(obj1, tileX, tileY, "adt obj1 " + tile));
            t.Lod = Part(maidWords, WdtTileFileRole.LodAdt, lod, () => ParseLod(lod, "adt lod " + tile));
            return t;
        }

        static AdtPartResult<T> Part<T>(uint[] maid, WdtTileFileRole role, byte[] bytes, Func<T> parse) where T : class
        {
            var r = new AdtPartResult<T>();
            if (bytes == null)
            {
                r.State = maid != null && maid.Length > (int)role && maid[(int)role] == 0 ? AdtPartState.NotNamed : AdtPartState.NotSupplied;
                return r;
            }
            if (bytes.Length == 0)
            {
                // A listed file that is not on this computer reads as empty (audit 3.3): not a malformed file.
                r.State = AdtPartState.NotSupplied;
                r.Error = "0 bytes supplied (an empty read: a listed file that is not on this computer opens empty, audit 3.3)";
                return r;
            }
            try
            {
                r.Value = parse();
                r.State = AdtPartState.Parsed;
            }
            catch (WowParseException e)
            {
                r.State = AdtPartState.Malformed;
                r.Error = e.Message;
            }
            return r;
        }

        // ================================================================================== helpers

        static List<WorldChunk> WalkFile(byte[] data, string what)
        {
            if (data == null)
                throw new WorldParseException(what, "file", 0, "no data");
            return WorldChunkWalker.Walk(data, 0, data.Length, what, "file");
        }

        static uint ReadVersion(byte[] data, List<WorldChunk> top, string what, List<WorldIssue> issues)
        {
            if (top.Count == 0 || top[0].Tag != "MVER")
                throw new WorldParseException(what, "MVER", 0, "the file does not start with an MVER chunk");
            var s = new WorldSpan(data, top[0], what);
            if (s.Size < 4)
                throw new WorldParseException(what, "MVER", top[0].DataOffset, "shorter than 4 bytes");
            uint v = s.U32(0);
            if (v != 18)
                issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MVER", top[0].DataOffset,
                                          "version " + v + " (every audited ADT part has 18)"));
            return v;
        }

        static List<WorldChunk> RequireMcnk(List<WorldChunk> top, string what)
        {
            var list = new List<WorldChunk>();
            foreach (WorldChunk c in top)
                if (c.Tag == "MCNK")
                    list.Add(c);
            if (list.Count != ChunksPerTile)
                throw new WorldParseException(what, "MCNK", list.Count > 0 ? list[0].HeaderOffset : 0, string.Format(
                    "{0} MCNK chunk(s); a tile has 256", list.Count));
            return list;
        }

        static WorldUnsupportedChunk Unsupported(WorldChunk c, string reason)
        {
            return new WorldUnsupportedChunk { Tag = WorldChunkWalker.Printable(c.Tag), DataOffset = c.DataOffset, Size = c.Size, Reason = reason };
        }
    }
}
