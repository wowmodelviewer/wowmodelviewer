// LiquidTests.cs
//
// Unit tests of the Stage 09 liquid layer on hand-built bytes (docs/world-viewer/09-liquids.md 1.2, 1.3, 1.5, 1.7):
// the MH2O decode in AdtParser (attributes, exists bitmaps, the four vertex formats by size, partial and multi-layer
// instances, every malformed structure reported per chunk or instance), LiquidFormat's rule, TerrainLiquidMesh
// (positions on the terrain's grid, counts, winding, uv0 and uv1, seams, counters), and WmoLiquid / WmoLiquidMesh (the
// MLIQ size equation, tiles, the group liquid type rule, the group-frame mesh). Byte builders: AdtSynthetic.Mh2o and
// WmoSynthetic.Mliq. Framework-free like the other suites: RunAll returns the failure count. The same code on real
// client bytes is LiquidCorpusTests.cs.

using System;
using System.Collections.Generic;
using System.Linq;
using Wmv.Wow;
using Wmv.Wow.World;
using static Wmv.Wow.World.AdtSynthetic;

namespace Wmv.Wow.Tests
{
    public static class LiquidTests
    {
        const int TX = 30, TY = 47;
        const double C = TerrainCoordinates.ChunkSize;
        const double U = TerrainCoordinates.UnitSize;

        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static AdtRoot Parse(Func<int, LiquidChunkSpec> chunks)
        {
            return AdtParser.ParseRoot(Root(TX, TY, null, Mh2o(chunks)), TX, TY);
        }

        static bool NoLiquidIssues(AdtRoot r)
        {
            return !r.Issues.Exists(i => i.Chunk != null && i.Chunk.StartsWith("MH2O", StringComparison.Ordinal));
        }

        static LiquidTypeView Type(int id, LiquidKind kind, int lvf, float float0 = 1f, float float1 = 0f)
        {
            return new LiquidTypeView { Id = id, Name = "type " + id, Kind = (int)kind, Lvf = lvf, Float0 = float0, Float1 = float1 };
        }

        static LiquidCatalog Catalog(params LiquidTypeView[] types)
        {
            var c = new LiquidCatalog();
            foreach (LiquidTypeView t in types) c.Add(t);
            return c;
        }

        /// <summary>Cross(p1 - p0, p2 - p0).y of every triangle, smallest (Unity positions).</summary>
        static double MinCrossY(LiquidSurface s)
        {
            double min = double.PositiveInfinity;
            float[] p = s.Positions;
            for (int t = 0; t < s.Indices.Length; t += 3)
            {
                int a = 3 * s.Indices[t], b = 3 * s.Indices[t + 1], c = 3 * s.Indices[t + 2];
                double e1x = p[b] - p[a], e1z = p[b + 2] - p[a + 2], e2x = p[c] - p[a], e2z = p[c + 2] - p[a + 2];
                min = Math.Min(min, e1z * e2x - e1x * e2z);
            }
            return min;
        }

        // ================================================================================== MH2O decode

        static float H0(int i, int j) { return 10f + i + 0.5f * j; }

        static void FormatTests()
        {
            AdtRoot r = Parse(n =>
            {
                switch (n)
                {
                    case 0: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = H0, DepthAt = (i, j) => (byte)(i * 9 + j) });
                    case 1: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 1, LiquidType = 7, HeightAt = H0, UAt = (i, j) => (ushort)(i * 100 + j), VAt = (i, j) => (ushort)(j * 100 + i) });
                    case 2: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 2, LiquidType = 2, MinHeight = 3f, MaxHeight = 3f, DepthAt = (i, j) => (byte)(200 + i) });
                    case 3: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 3, HeightAt = H0, UAt = (i, j) => (ushort)(i + 1), VAt = (i, j) => (ushort)(j + 2), DepthAt = (i, j) => (byte)(i + j) });
                    case 4: return LiquidChunkSpec.Of(new LiquidInstanceSpec { MinHeight = 56.25f, MaxHeight = 56.25f });
                    case 255: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 3, HeightAt = (i, j) => -7f, UAt = (i, j) => 5, VAt = (i, j) => 6, DepthAt = (i, j) => 9 });
                    default: return null;
                }
            });
            AdtLiquids l = r.Liquids;
            Check(l != null && l.Status == WorldDataStatus.PartiallyDecoded && l.MalformedInstances == 0 && NoLiquidIssues(r),
                  "formats: six instances decode with no issue; MH2O stays PartiallyDecoded (scales and object words are raw)");
            LiquidInstance f0 = l.Instances[0][0], f1 = l.Instances[1][0], f2 = l.Instances[2][0], f3 = l.Instances[3][0], none = l.Instances[4][0], last = l.Instances[255][0];
            bool ok0 = f0.FormatBySize == 0 && f0.VertexBlockBytes == 405 && f0.Heights.Length == 81 && f0.Depths.Length == 81 && f0.U == null && f0.V == null;
            for (int i = 0; i <= 8 && ok0; i++)
                for (int j = 0; j <= 8; j++)
                    ok0 &= f0.Heights[i * 9 + j] == H0(i, j) && f0.Depths[i * 9 + j] == i * 9 + j && f0.HeightAt(i, j) == H0(i, j);
            Check(ok0 && f0.MinHeight == 10f && f0.MaxHeight == 22f, "format 0 (5 B/vertex): heights then depths, row-major i * 9 + j; min/max of the used heights");
            bool ok1 = f1.FormatBySize == 1 && f1.VertexBlockBytes == 648 && f1.Depths == null;
            for (int i = 0; i <= 8 && ok1; i++)
                for (int j = 0; j <= 8; j++)
                    ok1 &= f1.Heights[i * 9 + j] == H0(i, j) && f1.U[i * 9 + j] == i * 100 + j && f1.V[i * 9 + j] == j * 100 + i;
            Check(ok1, "format 1 (8 B/vertex): heights, then (u, v) interleaved per vertex");
            bool ok2 = f2.FormatBySize == 2 && f2.VertexBlockBytes == 81 && f2.Heights == null && f2.U == null && f2.Depths[80] == 208 && f2.HeightAt(4, 4) == 3f;
            Check(ok2, "format 2 (1 B/vertex): depths only, every height is the instance's min");
            bool ok3 = f3.FormatBySize == 3 && f3.VertexBlockBytes == 729;
            for (int i = 0; i <= 8 && ok3; i++)
                for (int j = 0; j <= 8; j++)
                    ok3 &= f3.Heights[i * 9 + j] == H0(i, j) && f3.U[i * 9 + j] == i + 1 && f3.V[i * 9 + j] == j + 2 && f3.Depths[i * 9 + j] == i + j;
            Check(ok3, "format 3 (9 B/vertex): heights, (u, v) pairs, then depths");
            Check(none.FormatBySize == -1 && none.VertexBlockBytes == 0 && none.Heights == null && none.Depths == null && none.HeightAt(8, 8) == 56.25f &&
                  none.Exists.Length == 64 && none.Exists.All(e => e) && !none.HasExistsBitmap,
                  "no vertex data: format -1, flat at min, every quad exists without a bitmap");
            Check(last.FormatBySize == 3 && last.VertexBlockBytes == 729 && last.Heights[40] == -7f,
                  "the last structure in MH2O: its size is the gap to the end of the chunk");
            Check(f0.Chunk == 0 && f0.Layer == 0 && last.Chunk == 255 && f0.LiquidType == 5 && f0.ObjectOrFormat == 42 && f1.LiquidType == 7 &&
                  l.Instances[5].Length == 0, "record fields: chunk, layer, type, second word; chunks without liquid have no instances");
            // Packed layout: header table, 6 instances, then the data region in chunk order.
            int data = 3072 + 6 * 24;
            Check(l.Headers[0].InstancesOffset == 3072 && l.Headers[255].InstancesOffset == 3072 + 5 * 24 && f0.VertexOffset == data &&
                  f1.VertexOffset == data + 405 && f2.VertexOffset == data + 405 + 648 && l.Size == data + 405 + 648 + 81 + 729 + 729,
                  "synthetic MH2O: instances from 3,072 in chunk order, data packed in chunk order");
        }

        static void BitmapTests()
        {
            AdtRoot r = Parse(n => n == 9
                ? LiquidChunkSpec.Of(new LiquidInstanceSpec { X = 2, Y = 3, Width = 3, Height = 2, Format = 0, HeightAt = (i, j) => 5f + 10f * i + j,
                                                             Exists = (q, c) => (q + c) % 2 == 0, SpareBits = true })
                : n == 10 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Exists = (q, c) => q == 7 })
                : null);
            LiquidInstance p = r.Liquids.Instances[9][0], row = r.Liquids.Instances[10][0];
            Check(p.Status == WorldDataStatus.Decoded && p.HasExistsBitmap && p.ExistsSpareBits && p.Exists.Length == 6 &&
                  p.Exists.SequenceEqual(new[] { true, false, true, false, true, false }) && p.QuadExists(0, 0) && !p.QuadExists(0, 1) && p.QuadExists(1, 1) &&
                  !p.QuadExists(2, 0) && !p.QuadExists(0, 3), "partial 3 x 2 instance at cells (2, 3): instance-local bits r * 3 + c, spare bits masked and flagged");
            Check(p.VertexCount == 12 && p.FormatBySize == 0 && p.VertexBlockBytes == 60 && p.HeightAt(2, 3) == 28f && p.MinHeight == 5f && p.MaxHeight == 27f,
                  "partial instance: (w + 1)(h + 1) = 12 vertices of 5 bytes, vertex (i, j) at i * 4 + j");
            bool rowOk = row.HasExistsBitmap && !row.ExistsSpareBits;
            for (int q = 0; q < 64; q++)
                rowOk &= row.Exists[q] == (q >= 56);
            Check(rowOk, "full instance bitmap: bit r * 8 + c, least significant first (only row 7 set = bytes 0..6 zero, byte 7 0xFF)");
        }

        static void LayerAndAttributeTests()
        {
            AdtRoot r = Parse(n =>
            {
                if (n == 6)
                    return new LiquidChunkSpec
                    {
                        Attributes = true, Fishable = 0xFFUL | (1UL << 63), Deep = 1UL << 63,
                        Layers = new[]
                        {
                            new LiquidInstanceSpec { Format = 0, HeightAt = (i, j) => 20f },
                            new LiquidInstanceSpec { LiquidType = 7, Format = 1, HeightAt = (i, j) => 30f, X = 1, Width = 2, Exists = (q, c) => c == 0 },
                            new LiquidInstanceSpec { LiquidType = 2, MinHeight = 40f, MaxHeight = 40f },
                        },
                    };
                if (n == 7)
                    return new LiquidChunkSpec { Attributes = true, Fishable = 1, Deep = 2, Layers = new[] { new LiquidInstanceSpec() } };
                return n == 8 ? LiquidChunkSpec.Of(new LiquidInstanceSpec()) : null;
            });
            AdtLiquids l = r.Liquids;
            LiquidInstance[] ls = l.Instances[6];
            Check(ls.Length == 3 && ls[0].Layer == 0 && ls[1].Layer == 1 && ls[2].Layer == 2 && ls[0].FormatBySize == 0 && ls[1].FormatBySize == 1 &&
                  ls[2].FormatBySize == -1 && ls[1].Exists.Count(e => e) == 8 && ls.All(i => i.Status == WorldDataStatus.Decoded),
                  "three layers in one chunk: each decodes its own bitmap and vertex block, in layer order");
            Check(l.Attributes[6].Present && l.Attributes[6].Fishable == (0xFFUL | (1UL << 63)) && l.Attributes[6].Deep == 1UL << 63 &&
                  !l.Attributes[8].Present && !l.Attributes[0].Present,
                  "attributes: two u64 masks, cell (row, col) = bit row * 8 + col; absent when the offset is 0");
            Check(l.Attributes[7].Present && r.Issues.Exists(i => i.Kind == WorldIssueKind.Unexpected && i.Chunk == "MH2O chunk 7" && i.Reason.Contains("subset")),
                  "attributes: a second mask that is not a subset of the first is kept and flagged Unexpected");
            Check(ls[0].ExistsOffset == 0 && ls[1].ExistsOffset == 3072 + 5 * 24 + 16 + 405 && ls[1].VertexOffset == ls[1].ExistsOffset + 2,
                  "data region: attributes (16 bytes), then per layer bitmap then vertex data");
        }

        static void MalformedTests()
        {
            // A block of the wrong size: 3 extra bytes (408 is not a multiple of 81), 2 and 4 bytes per vertex.
            AdtRoot r = Parse(n =>
            {
                switch (n)
                {
                    case 0: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = H0, VertexExtraBytes = 3 });
                    case 1: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = H0 });
                    case 2: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 2, VertexExtraBytes = 81 });
                    case 3: return LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 2, VertexExtraBytes = 243 });
                    default: return null;
                }
            });
            AdtLiquids l = r.Liquids;
            Check(l.Status == WorldDataStatus.PartiallyDecoded && l.MalformedInstances == 3 && l.Instances[0][0].Status == WorldDataStatus.Malformed &&
                  l.Instances[0][0].Heights == null && l.Instances[0][0].FormatBySize == -1 && l.Instances[1][0].Status == WorldDataStatus.Decoded &&
                  l.Instances[2][0].Problem.Contains("not 1, 5, 8 or 9") && l.Instances[3][0].Problem.Contains("not 1, 5, 8 or 9") &&
                  r.Issues.Exists(i => i.Kind == WorldIssueKind.Malformed && i.Chunk == "MH2O chunk 0 layer 0" && i.Reason.Contains("408 byte(s)")) &&
                  r.Mcnk[0].Status == WorldDataStatus.Decoded,
                  "vertex block sizes 408 B, 2 and 4 B per vertex: those instances Malformed with the reason, the others and the terrain decoded");

            // Offsets that point at the wrong place: the header table, an instance table, past the end, overlaps, shared.
            Func<LiquidInstanceSpec, AdtRoot> one = spec => Parse(n => n == 0 ? LiquidChunkSpec.Of(spec) : null);
            AdtRoot hdr = one(new LiquidInstanceSpec { Format = 0, VertexOffset = 100 });
            Check(hdr.Liquids.MalformedInstances == 1 && hdr.Liquids.Instances[0][0].Problem.Contains("header table"), "vertex data inside the header table is malformed");
            AdtRoot tbl = one(new LiquidInstanceSpec { Format = 0, VertexOffset = 3072 + 4 });
            Check(tbl.Liquids.Instances[0][0].Problem.Contains("instance table"), "vertex data inside an instance table is malformed");
            AdtRoot end = one(new LiquidInstanceSpec { Format = 0, VertexOffset = 0xFFFFFFF0 });
            Check(end.Liquids.Instances[0][0].Problem.Contains("past the end"), "vertex data past the end of MH2O is malformed (no overflow)");
            AdtRoot bmEnd = one(new LiquidInstanceSpec { Exists = (q, c) => true, ExistsOffset = 0xFFFFFFFF });
            Check(bmEnd.Liquids.Instances[0][0].Problem.Contains("past the end"), "an exists bitmap past the end of MH2O is malformed");
            // One instance at 3,072, data from 3,096: the bitmap's 8 bytes at 3,096, the vertex block at 3,104.
            AdtRoot overlap = one(new LiquidInstanceSpec { Format = 0, Exists = (q, c) => true, ExistsOffset = 3100 });
            Check(overlap.Liquids.Instances[0][0].Problem.Contains("overlap"), "an exists bitmap running into the vertex block is malformed");
            AdtRoot shared = Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0 }, new LiquidInstanceSpec { Format = 0, VertexOffset = 3120 }) : null);
            Check(shared.Liquids.MalformedInstances == 2 && shared.Liquids.Instances[0].All(i => i.Problem.Contains("shares its offset")),
                  "two vertex blocks at one offset: both malformed (a shared start's size cannot be measured)");

            // An offset inside a well-formed neighbour's block: chunk 0's format-0 block at P (405 bytes), chunk 1's vertex data
            // pointed at P + 81 (the gap would make chunk 0 depth-only) or P + 324 (4 bytes a vertex: chunk 0 malformed). The
            // packing decides whose the offset is: chunk 1's attributes lie between them in chunk order, or chunk 0's own word 0
            // gives its 405 bytes. Chunk 1 is malformed; chunk 0 keeps its format, its bytes and its heights.
            const uint P = 3072 + 2 * 24;
            foreach (uint into in new uint[] { 81, 324 })
            {
                AdtRoot ordered = Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = H0 })
                                       : n == 1 ? new LiquidChunkSpec { Attributes = true, Layers = new[] { new LiquidInstanceSpec { Format = 0, HeightAt = H0, VertexOffset = P + into } } }
                                       : null);
                AdtRoot owned = Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { ObjectOrFormat = 0, Format = 0, HeightAt = H0 })
                                     : n == 1 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = H0, VertexOffset = P + into })
                                     : null);
                foreach (AdtRoot x in new[] { ordered, owned })
                {
                    bool isOrdered = x == ordered;
                    LiquidInstance a = x.Liquids.Instances[0][0], b = x.Liquids.Instances[1][0];
                    bool heights = a.Heights != null && a.Heights.Length == 81;
                    for (int i = 0; i <= 8 && heights; i++)
                        for (int j = 0; j <= 8; j++)
                            heights &= a.Heights[i * 9 + j] == H0(i, j);
                    Check(a.Status == WorldDataStatus.Decoded && a.VertexOffset == P && a.FormatBySize == 0 && a.VertexBlockBytes == 405 && heights &&
                          a.Depths != null && b.Status == WorldDataStatus.Malformed && b.FormatBySize == -1 && b.Heights == null &&
                          b.Problem.Contains(isOrdered ? "out of the chunk order" : "lies inside the 405 byte(s)") && x.Liquids.MalformedInstances == 1 &&
                          (!isOrdered || (x.Liquids.Attributes[1].Present && x.Liquids.MalformedAttributes == 0)),
                          "vertex data pointed " + into + " bytes into a well-formed neighbour's block (" + (isOrdered ? "chunk order" : "the neighbour's own word") +
                          "): the offender malformed, the neighbour still format 0, 405 bytes, its heights (" + a.Status + " " + a.FormatBySize + "/" +
                          a.VertexBlockBytes + "; " + (b.Problem ?? "chunk 1 decoded") + ")");
                }
            }
            // A start that jumped ahead (chunk 0's vertex data pointed 100 bytes into chunk 2's, past chunk 1's attributes and
            // vertex data): it goes, they stay, and chunk 2 keeps its 405 bytes.
            const uint P2 = 3072 + 3 * 24 + 405 + 16 + 405;
            AdtRoot ahead = Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = H0, VertexOffset = P2 + 100 })
                                  : n == 1 ? new LiquidChunkSpec { Attributes = true, Layers = new[] { new LiquidInstanceSpec { Format = 0, HeightAt = H0 } } }
                                  : n == 2 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = H0 })
                                  : null);
            Check(ahead.Liquids.Instances[0][0].Status == WorldDataStatus.Malformed && ahead.Liquids.Instances[0][0].Problem.Contains("out of the chunk order") &&
                  ahead.Liquids.Instances[1][0].Status == WorldDataStatus.Decoded && ahead.Liquids.Instances[1][0].FormatBySize == 0 &&
                  ahead.Liquids.Instances[2][0].Status == WorldDataStatus.Decoded && ahead.Liquids.Instances[2][0].VertexOffset == P2 &&
                  ahead.Liquids.Instances[2][0].VertexBlockBytes == 405 && ahead.Liquids.Attributes[1].Present && ahead.Liquids.MalformedInstances == 1,
                  "vertex data that jumped ahead of the structures after it in chunk order: it is malformed, they decode");
            // The instance's own rule against its block's size: the word (0, 1 or 2) or the ocean type decides; another size is
            // malformed, never decoded by the size.
            AdtRoot wordConflict = one(new LiquidInstanceSpec { ObjectOrFormat = 0, Format = 2 });
            AdtRoot oceanConflict = one(new LiquidInstanceSpec { LiquidType = 2, Format = 0, HeightAt = H0 });
            Check(wordConflict.Liquids.Instances[0][0].Status == WorldDataStatus.Malformed &&
                  wordConflict.Liquids.Instances[0][0].Problem.Contains("format 2 by its size, but format 0 by its own second word 0") &&
                  oceanConflict.Liquids.Instances[0][0].Problem.Contains("format 0 by its size, but format 2 by its own liquid type 2"),
                  "a vertex block whose size gives another format than the instance's own word or ocean type is malformed (" +
                  wordConflict.Liquids.Instances[0][0].Problem + "; " + oceanConflict.Liquids.Instances[0][0].Problem + ")");

            // Records that cannot be drawn.
            AdtRoot cells = one(new LiquidInstanceSpec { X = 6, Width = 4 });
            Check(cells.Liquids.Instances[0][0].Problem.Contains("8 x 8"), "an instance past the chunk's 8 x 8 cells is malformed");
            AdtRoot empty = one(new LiquidInstanceSpec { Width = 0 });
            Check(empty.Liquids.Instances[0][0].Status == WorldDataStatus.Malformed, "an instance of width 0 is malformed");
            AdtRoot nanMin = one(new LiquidInstanceSpec { MinHeight = float.NaN, MaxHeight = 1f });
            Check(nanMin.Liquids.Instances[0][0].Problem.Contains("not a usable height"), "a non-finite min height is malformed");
            AdtRoot nanUsed = one(new LiquidInstanceSpec { Format = 0, HeightAt = (i, j) => i == 0 && j == 0 ? float.NaN : 1f, MinHeight = 1f, MaxHeight = 1f });
            Check(nanUsed.Liquids.Instances[0][0].Problem.Contains("vertex (0, 0)"), "a non-finite height at a used vertex is malformed");
            AdtRoot nanUnused = one(new LiquidInstanceSpec { Format = 0, HeightAt = (i, j) => i == 0 && j == 0 ? float.NaN : 1f, Exists = (q, c) => q > 0 || c > 0 });
            Check(nanUnused.Liquids.Instances[0][0].Status == WorldDataStatus.Decoded && NoLiquidIssues(nanUnused),
                  "a non-finite height at a vertex no existing quad uses is a leftover: decoded, no issue");
            AdtRoot outside = one(new LiquidInstanceSpec { Format = 0, HeightAt = (i, j) => 5f, MinHeight = 0f, MaxHeight = 1f });
            Check(outside.Liquids.Instances[0][0].Status == WorldDataStatus.Decoded &&
                  outside.Issues.Exists(i => i.Kind == WorldIssueKind.Unexpected && i.Reason.Contains("81 used vertex height(s) outside")),
                  "heights outside [min, max] are kept and flagged Unexpected");
            AdtRoot noQuad = one(new LiquidInstanceSpec { Exists = (q, c) => false });
            Check(noQuad.Liquids.Instances[0][0].Status == WorldDataStatus.Decoded && noQuad.Issues.Exists(i => i.Reason.Contains("marks no quad")),
                  "an all-zero bitmap is kept and flagged Unexpected");
            AdtRoot minMax = one(new LiquidInstanceSpec { MinHeight = 2f, MaxHeight = 1f });
            Check(minMax.Issues.Exists(i => i.Kind == WorldIssueKind.Unexpected && i.Reason.Contains("above max")), "min above max is flagged Unexpected");
            AdtRoot attr = Parse(n => n == 0 ? new LiquidChunkSpec { AttributesOffset = 8, Layers = new[] { new LiquidInstanceSpec() } } : null);
            Check(attr.Liquids.MalformedAttributes == 1 && !attr.Liquids.Attributes[0].Present && attr.Liquids.Instances[0][0].Status == WorldDataStatus.Decoded &&
                  attr.Issues.Exists(i => i.Chunk == "MH2O chunk 0" && i.Reason.Contains("attributes")),
                  "attributes inside the header table: reported for the chunk, its instances unaffected");

            // Random corruption of a valid MH2O never throws from the parser or the mesh builder.
            byte[] good = Mh2oPayload(n => n < 4 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = n, HeightAt = H0, Exists = (q, c) => (q * c) % 3 != 1 }) : null);
            var rnd = new Random(9);
            int thrown = 0, malformed = 0;
            for (int t = 0; t < 400; t++)
            {
                byte[] bad = (byte[])good.Clone();
                for (int k = 0; k < 4; k++)
                    bad[3072 + rnd.Next(bad.Length - 3072)] = (byte)rnd.Next(256);
                try
                {
                    AdtRoot cr = AdtParser.ParseRoot(Root(TX, TY, null, Chunk("MH2O", bad)), TX, TY);
                    TerrainLiquidMesh.Build(cr.Liquids, TX, TY, WorldToUnity.TileOrigin(TX, TY), null);
                    malformed += cr.Liquids.MalformedInstances;
                }
                catch (Exception)
                {
                    thrown++;
                }
            }
            Check(thrown == 0 && malformed > 0, "400 randomly corrupted MH2O data regions: no exception from the parser or the mesh builder (" + malformed + " malformed instances reported)");
        }

        // ================================================================================== format rule

        static LiquidInstance Inst(ushort type, ushort word, int formatBySize)
        {
            return new LiquidInstance { LiquidType = type, ObjectOrFormat = word, FormatBySize = formatBySize, Width = 8, Height = 8 };
        }

        static void FormatRuleTests()
        {
            Check(LiquidFormat.BytesPerVertex(0) == 5 && LiquidFormat.BytesPerVertex(1) == 8 && LiquidFormat.BytesPerVertex(2) == 1 && LiquidFormat.BytesPerVertex(3) == 9 &&
                  LiquidFormat.BytesPerVertex(4) == 0 && LiquidFormat.FormatOfBytesPerVertex(5) == 0 && LiquidFormat.FormatOfBytesPerVertex(8) == 1 &&
                  LiquidFormat.FormatOfBytesPerVertex(1) == 2 && LiquidFormat.FormatOfBytesPerVertex(9) == 3 &&
                  new[] { 0, 2, 3, 4, 6, 7, 10 }.All(b => LiquidFormat.FormatOfBytesPerVertex(b) == -1),
                  "formats: 5, 8, 1, 9 bytes per vertex <=> 0, 1, 2, 3; every other size (4 = height only) is none");
            Check(LiquidFormat.HasHeights(0) && LiquidFormat.HasHeights(1) && !LiquidFormat.HasHeights(2) && LiquidFormat.HasHeights(3) &&
                  !LiquidFormat.HasUv(0) && LiquidFormat.HasUv(1) && !LiquidFormat.HasUv(2) && LiquidFormat.HasUv(3) &&
                  LiquidFormat.HasDepth(0) && !LiquidFormat.HasDepth(1) && LiquidFormat.HasDepth(2) && LiquidFormat.HasDepth(3) && !LiquidFormat.HasHeights(-1),
                  "formats: which arrays each carries");
            LiquidCatalog cat = Catalog(Type(5, LiquidKind.Water, 0), Type(7, LiquidKind.Magma, 1), Type(8, LiquidKind.Slime, -1), Type(2, LiquidKind.Ocean, 0));
            Func<LiquidInstance, ILiquidCatalog, int, LiquidFormatSource, bool, bool> rule = (i, c, f, s, dis) =>
            {
                LiquidFormatDecision d = LiquidFormat.Decide(i, c);
                return d.Format == f && d.Source == s && d.Disagrees == dis && LiquidFormat.Resolve(i, c) == f;
            };
            Check(rule(Inst(2, 0, 0), cat, 0, LiquidFormatSource.Word, false) && rule(Inst(7, 1, 1), cat, 1, LiquidFormatSource.Word, false) &&
                  rule(Inst(5, 2, -1), cat, 2, LiquidFormatSource.Word, false) && rule(Inst(5, 2, 0), cat, 2, LiquidFormatSource.Word, true),
                  "rule: a second word of 0, 1 or 2 is the format, over the type and the catalogue; no data is no disagreement");
            Check(rule(Inst(2, 42, 2), cat, 2, LiquidFormatSource.OceanType, false) && rule(Inst(2, 42, -1), cat, 2, LiquidFormatSource.OceanType, false) &&
                  rule(Inst(2, 57, 0), cat, 2, LiquidFormatSource.OceanType, true),
                  "rule: liquid type 2 is format 2 (or no data) whatever its material");
            Check(rule(Inst(5, 5987, 0), cat, 0, LiquidFormatSource.Catalog, false) && rule(Inst(7, 600, 1), cat, 1, LiquidFormatSource.Catalog, false) &&
                  rule(Inst(7, 600, 0), cat, 1, LiquidFormatSource.Catalog, true),
                  "rule: otherwise the catalogue's LVF; a different size is a disagreement");
            Check(rule(Inst(9, 600, 3), cat, 3, LiquidFormatSource.Size, false) && rule(Inst(8, 600, 1), cat, 1, LiquidFormatSource.Size, false) &&
                  rule(Inst(5, 600, 0), null, 0, LiquidFormatSource.Size, false) && rule(Inst(9, 600, -1), cat, -1, LiquidFormatSource.None, false),
                  "rule: a type the catalogue lacks (or without a material, or no catalogue) takes the size; with no data, none (-1)");
        }

        // ================================================================================== terrain liquid mesh

        static void TerrainMeshTests()
        {
            const int chunk = 17;                                   // ix 1, iy 1
            Func<int, int, float> hf = (i, j) => 60f + 0.125f * i;
            AdtRoot r = AdtParser.ParseRoot(Root(TX, TY, null, Mh2o(n => n == chunk ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = hf, DepthAt = (i, j) => (byte)(i * 30) }) : null)), TX, TY);
            WorldOrigin o = WorldToUnity.TileOrigin(TX, TY);
            TerrainTileMesh terrain = TerrainMeshBuilder.Build(r, TX, TY, o);
            TerrainLiquidTile t = TerrainLiquidMesh.Build(r.Liquids, TX, TY, o, null);
            LiquidSurface s = t.Surfaces.Count == 1 ? t.Surfaces[0] : null;
            Check(s != null && s.LiquidType == 5 && s.Type == null && s.VertexCount == 81 && s.QuadCount == 64 && s.Indices.Length == 384 && s.Instances == 1 &&
                  t.Quads == 64 && t.Vertices == 81 && t.ChunksWithLiquid == 1 && t.Instances == 1 && t.InstancesWithQuads == 1 && t.MaxLayers == 1 &&
                  t.Types.SequenceEqual(new[] { 5 }) && t.MissingTypes == 1 && t.FormatFromSize == 1 && t.FormatDisagreements == 0 &&
                  s.MeshBytes == 81 * 28 + 384 * 2 && t.GeometryBytes == s.MeshBytes,
                  "full instance: 81 shared vertices, 64 quads, one surface; no catalogue = the type missing, the format from the size");
            // Every vertex is bit-exactly over the terrain's outer grid point (row i, column j) of the same chunk.
            var grid = new Dictionary<long, int>();
            float[] tp = terrain.Chunks[chunk].Positions;
            for (int rr = 0; rr <= 8; rr++)
                for (int cc = 0; cc <= 8; cc++)
                {
                    int k = TerrainVertexLayout.Outer(rr, cc);
                    grid[Key(tp[3 * k], tp[3 * k + 2])] = rr * 9 + cc;
                }
            int onGrid = 0, heightOk = 0, depthOk = 0;
            for (int v = 0; v < s.VertexCount; v++)
            {
                int rc;
                if (!grid.TryGetValue(Key(s.Positions[3 * v], s.Positions[3 * v + 2]), out rc))
                    continue;
                onGrid++;
                if (s.Positions[3 * v + 1] == hf(rc / 9, rc % 9)) heightOk++;
                if (s.Uv1[2 * v] == (rc / 9) * 30 / 255f && s.Uv1[2 * v + 1] == 1f) depthOk++;
            }
            Check(onGrid == 81 && heightOk == 81, "positions: x and z bit-equal to the terrain's outer vertex (row y + i, column x + j); y = the stored height");
            Check(depthOk == 81, "uv1 = (depth / 255, 1) where the vertex stores a depth");
            Check(MinCrossY(s) > 0 && Math.Abs(MinCrossY(s) - U * U) < 1e-3, "winding: every triangle's Cross.y is U^2 > 0 (front faces up, as the terrain's)");
            Check(s.BoundsMin[1] == 60f && s.BoundsMax[1] == 61f && Math.Abs(s.BoundsMin[0] - (float)C) < 1e-3 && Math.Abs(s.BoundsMax[0] - (float)(2 * C)) < 1e-3,
                  "bounds over the vertices (chunk 17 spans ux C..2C)");

            // Partial instance with a bitmap: only the vertices an existing quad uses.
            AdtRoot pr = Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { X = 2, Y = 3, Width = 3, Height = 2, Exists = (q, c) => (q + c) % 2 == 0, MinHeight = 7f, MaxHeight = 7f }) : null);
            TerrainLiquidTile pt = TerrainLiquidMesh.Build(pr.Liquids, TX, TY, o, null);
            LiquidSurface ps = pt.Surfaces[0];
            Check(ps.QuadCount == 3 && ps.VertexCount == 10 && ps.Uv1.Where((x, i) => i % 2 == 0).All(x => x == 1f) && ps.Uv1.Where((x, i) => i % 2 == 1).All(x => x == 0f) &&
                  ps.Positions.Where((x, i) => i % 3 == 1).All(y => y == 7f),
                  "partial instance, quads (0,0) (0,2) (1,1): 3 quads, the 10 vertices they use; no data = flat at min, uv1 (1, 0)");
            double x0, y0;
            TerrainMeshBuilder.VertexWorldXY(TX, TY, 0, 0, TerrainVertexLayout.Outer(3, 2), out x0, out y0);
            float ex, ey, ez;
            WorldToUnity.Position(x0, y0, 7f, o, out ex, out ey, out ez);
            Check(ps.Positions[0] == ex && ps.Positions[2] == ez, "partial instance: its vertex (0, 0) is the chunk's outer vertex (row 3, column 2)");

            // Multi-layer and multi-type: one surface per type, in first-use order.
            AdtRoot mr = Parse(n => n == 40 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { LiquidType = 7, Format = 1, HeightAt = (i, j) => 1f }, new LiquidInstanceSpec { MinHeight = 2f, MaxHeight = 2f })
                                  : n == 3 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { MinHeight = 3f, MaxHeight = 3f, Width = 2, Height = 2 }) : null);
            TerrainLiquidTile mt = TerrainLiquidMesh.Build(mr.Liquids, TX, TY, o, Catalog(Type(5, LiquidKind.Water, 0), Type(7, LiquidKind.Magma, 1)));
            Check(mt.Surfaces.Count == 2 && mt.Surfaces[0].LiquidType == 5 && mt.Surfaces[1].LiquidType == 7 && mt.Surfaces[0].Instances == 2 &&
                  mt.Surfaces[0].QuadCount == 68 && mt.Surfaces[0].VertexCount == 81 + 9 && mt.Surfaces[1].QuadCount == 64 && mt.MultiLayerChunks == 1 &&
                  mt.MaxLayers == 2 && mt.Instances == 3 && mt.ChunksWithLiquid == 2 && mt.MissingTypes == 0 && mt.FormatFromSize == 0 &&
                  mt.Types.SequenceEqual(new[] { 5, 7 }) && mt.Surfaces[0].Type != null,
                  "layers: two types in one chunk make two surfaces; a type in two chunks shares one surface, vertices not shared across instances");

            // Planar uv0 (water): Float0 repeats per chunk east (u) and north (v), Float1 turns them; shifted to [0, 1) at the reference.
            AdtRoot wr = Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 3, HeightAt = (i, j) => 1f, UAt = (i, j) => 999 }) : null);
            LiquidSurface w2 = TerrainLiquidMesh.Build(wr.Liquids, TX, TY, o, Catalog(Type(5, LiquidKind.Water, 3, 2f, 0f))).Surfaces[0];
            float[] v00 = UvAt(w2, 0, 0, o), v08 = UvAt(w2, 0, 8, o), v80 = UvAt(w2, 8, 0, o);
            double pu, pv;
            TerrainLiquidMesh.PlanarUv(TerrainCoordinates.XOfRow(128 * TY), TerrainCoordinates.YOfColumn(128 * TX), 2, 0, out pu, out pv);
            Check(w2.PlanarUvInstances == 1 && w2.StoredUvInstances == 0 && Math.Abs(v08[0] - v00[0] - 2) < 1e-4 && Math.Abs(v08[1] - v00[1]) < 1e-4 &&
                  Math.Abs(v80[1] - v00[1] + 2) < 1e-4 && Math.Abs(v80[0] - v00[0]) < 1e-4 && v00[0] >= 0 && v00[0] <= 1 &&
                  Math.Abs(v00[0] - (pu - Math.Floor(pu))) < 1e-6 && Math.Abs(v00[1] - (pv - Math.Floor(pv))) < 1e-6,
                  "planar uv0 (water, even with stored u/v): Float0 = 2 repeats per chunk, u east, v north, world-frame value shifted by a whole number");
            LiquidSurface w90 = TerrainLiquidMesh.Build(wr.Liquids, TX, TY, o, Catalog(Type(5, LiquidKind.Ocean, 3, 2f, 90f))).Surfaces[0];
            float[] r00 = UvAt(w90, 0, 0, o), r08 = UvAt(w90, 0, 8, o), r80 = UvAt(w90, 8, 0, o);
            Check(Math.Abs(r80[0] - r00[0] - 2) < 1e-4 && Math.Abs(r08[1] - r00[1] - 2) < 1e-4 && Math.Abs(r08[0] - r00[0]) < 1e-4,
                  "planar uv0: Float1 = 90 degrees turns east into v and north into -u");
            LiquidSurface wd = TerrainLiquidMesh.Build(Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec()) : null).Liquids, TX, TY, o, null).Surfaces[0];
            Check(Math.Abs(UvAt(wd, 0, 8, o)[0] - UvAt(wd, 0, 0, o)[0] - (float)TerrainLiquidMesh.DefaultRepeatsPerChunk) < 1e-4,
                  "planar uv0 for a type the catalogue lacks: DefaultRepeatsPerChunk");
            // Planar uv0 does not depend on the origin (only positions do).
            WorldOrigin far = new WorldOrigin(o.X - 3 * TerrainCoordinates.TileSize, o.Y + 5 * TerrainCoordinates.TileSize, 17);
            LiquidSurface wf = TerrainLiquidMesh.Build(wr.Liquids, TX, TY, far, Catalog(Type(5, LiquidKind.Water, 3, 2f, 0f))).Surfaces[0];
            Check(wf.Uv0.SequenceEqual(w2.Uv0) && Math.Abs(wf.Positions[1] - (1f - 17f)) < 1e-6, "uv0 is the same for another origin; positions move with it");

            // Stored uv0 (magma, format 1): raw u16 x StoredUvScale, a wrap inside the instance joined.
            AdtRoot sr = Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { LiquidType = 7, Format = 1, HeightAt = (i, j) => 1f,
                                                                                         UAt = (i, j) => (ushort)((65500 + 10 * j) & 0xFFFF), VAt = (i, j) => (ushort)(100 * i) }) : null);
            LiquidSurface st = TerrainLiquidMesh.Build(sr.Liquids, TX, TY, o, Catalog(Type(7, LiquidKind.Magma, 1))).Surfaces[0];
            bool stOk = st.StoredUvInstances == 1 && st.PlanarUvInstances == 0;
            float[] s00 = UvAt(st, 0, 0, o);
            for (int j = 0; j <= 8; j++)
                stOk &= Math.Abs(UvAt(st, 0, j, o)[0] - s00[0] - 10f * j / 256f) < 1e-5;
            for (int i = 0; i <= 8; i++)
                stOk &= Math.Abs(UvAt(st, i, 0, o)[1] - s00[1] - 100f * i / 256f) < 1e-5;
            Check(stOk && Math.Abs(s00[0] - (65500 / 256.0 - 255)) < 1e-5 && s00[1] == 0f,
                  "stored uv0 (magma, u16 u/v): value x 1/256, continuous across the u16 wrap at 65536, shifted by a whole number");
            LiquidSurface stMissing = TerrainLiquidMesh.Build(sr.Liquids, TX, TY, o, null).Surfaces[0];
            Check(stMissing.StoredUvInstances == 1, "a type the catalogue lacks uses its stored u/v");

            // Counters: formats from the size, disagreements, malformed instances.
            AdtRoot cr = Parse(n => n == 0 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = H0 }, new LiquidInstanceSpec { LiquidType = 9, Format = 2 })
                                  : n == 1 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, VertexExtraBytes = 1 }) : null);
            TerrainLiquidTile ct = TerrainLiquidMesh.Build(cr.Liquids, TX, TY, o, Catalog(Type(5, LiquidKind.Water, 1)));
            Check(ct.FormatDisagreements == 1 && ct.FormatFromSize == 1 && ct.MalformedInstances == 1 && ct.MissingTypes == 1 && ct.InstancesWithQuads == 2 &&
                  ct.Surfaces.Sum(x => x.QuadCount) == 128,
                  "counters: catalogue LVF 1 against a 5-byte block = 1 disagreement (drawn by the bytes); type 9 missing = format from the size; the malformed instance not drawn");

            // Seams: two neighbouring chunks of one height field meet bit-exactly.
            Func<int, Func<int, int, float>> field = n => (i, j) => 30f + 0.5f * ((n / 16) * 8 + i) + 0.25f * ((n % 16) * 8 + j);
            AdtRoot seam = Parse(n => n == 40 || n == 41 || n == 56 ? LiquidChunkSpec.Of(new LiquidInstanceSpec { Format = 0, HeightAt = field(n) }) : null);
            LiquidSurface ss = TerrainLiquidMesh.Build(seam.Liquids, TX, TY, o, null).Surfaces[0];
            var seen = new Dictionary<long, float>();
            int shared = 0, equal = 0;
            for (int v = 0; v < ss.VertexCount; v++)
            {
                long key = Key(ss.Positions[3 * v], ss.Positions[3 * v + 2]);
                float y;
                if (seen.TryGetValue(key, out y))
                {
                    shared++;
                    if (y == ss.Positions[3 * v + 1]) equal++;
                }
                else
                    seen[key] = ss.Positions[3 * v + 1];
            }
            Check(ss.VertexCount == 3 * 81 && shared == 18 && equal == 18,
                  "seams: chunks 40 | 41 and 40 / 56 share 9 + 9 edge points, bit-equal in x, z and height; vertices not shared across chunks");

            // Inputs.
            Check(TerrainLiquidMesh.Build(null, TX, TY, o, null).Surfaces.Count == 0, "no MH2O: an empty liquid tile");
            bool threw = false;
            try { TerrainLiquidMesh.Build(r.Liquids, 64, 0, o, null); } catch (ArgumentOutOfRangeException) { threw = true; }
            Check(threw, "a tile outside 0..63 is rejected");
        }

        static long Key(float x, float z)
        {
            return ((long)BitConverter.ToInt32(BitConverter.GetBytes(x), 0) << 32) | (uint)BitConverter.ToInt32(BitConverter.GetBytes(z), 0);
        }

        /// <summary>uv0 of the surface vertex over chunk 0's outer grid point (row i, column j).</summary>
        static float[] UvAt(LiquidSurface s, int i, int j, WorldOrigin o)
        {
            double x, y;
            TerrainMeshBuilder.VertexWorldXY(TX, TY, 0, 0, TerrainVertexLayout.Outer(i, j), out x, out y);
            float ux, uy, uz;
            WorldToUnity.Position(x, y, 0, o, out ux, out uy, out uz);
            for (int v = 0; v < s.VertexCount; v++)
                if (s.Positions[3 * v] == ux && s.Positions[3 * v + 2] == uz)
                    return new[] { s.Uv0[2 * v], s.Uv0[2 * v + 1] };
            return new[] { float.NaN, float.NaN };
        }

        // ================================================================================== MLIQ

        static WmoSynthetic.LiquidSpec SampleMliq()
        {
            // X 4, Y 3: tiles 3 x 2; tile (1, 1) absent (0x4F), tile (2, 0) absent with bit 0x80 (0x8F); the rest nibble 4 with 0x40.
            return new WmoSynthetic.LiquidSpec
            {
                VerticesX = 4, VerticesY = 3, Corner = new WowVec3(10f, 20f, -5f), Material = 7,
                Height = (i, j) => -5f + 0.25f * i + 0.5f * j,
                Word = (i, j) => WmoSynthetic.LiquidDepthWord((byte)(i + 10 * j)),
                Tile = (i, j) => i == 1 && j == 1 ? (byte)0x4F : i == 2 && j == 0 ? (byte)0x8F : (byte)0x44,
            };
        }

        static WmoLiquid ParseMliq(byte[] payload, out string problem)
        {
            return WmoLiquid.Parse(payload, 0, payload.Length, out problem);
        }

        static void MliqParseTests()
        {
            string problem;
            WmoLiquid l = ParseMliq(WmoSynthetic.Mliq(SampleMliq()), out problem);
            Check(l != null && problem == null && l.VerticesX == 4 && l.VerticesY == 3 && l.TilesX == 3 && l.TilesY == 2 && l.Corner.X == 10f && l.Corner.Y == 20f &&
                  l.Corner.Z == -5f && l.Material == 7 && l.Heights.Length == 12 && l.Tiles.Length == 6 && l.Height(3, 2) == -5f + 0.75f + 1f &&
                  l.Depth(2, 1) == 12 && l.VertexWords[l.VertexIndex(2, 1)] == 12u && l.VertexIndex(2, 1) == 6,
                  "MLIQ: header X 4 Y 3 A 3 B 2, corner, material, vertex (i, j) at j X + i with its word and height");
            Check(l.PresentTiles == 4 && l.Nibble == 4 && !l.MixedNibbles && l.Present(0, 0) && !l.Present(1, 1) && !l.Present(2, 0) && l.Present(2, 1) &&
                  !l.Present(-1, 0) && !l.Present(3, 0) && !l.Present(0, 2),
                  "MLIQ tiles: low nibble 0xF = absent whatever the high bits; one nibble value over the present tiles");
            Check(l.VertexUsed(0, 0) && l.VertexUsed(3, 2) && !l.VertexUsed(3, 0) && l.VertexUsed(2, 2),
                  "MLIQ: a vertex is used when a present tile touches it");
            WmoLiquid st = ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { Word = (i, j) => WmoSynthetic.LiquidStWord((short)(-300 * i), (short)(1000 * j)) }), out problem);
            Check(st.S(2, 1) == -600 && st.T(2, 1) == 1000 && st.S(0, 0) == 0, "MLIQ: the s/t reading of the same four bytes, two i16");

            ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { ExtraBytes = 1 }), out problem);
            Check(problem != null && problem.Contains("not 30 + 8 X Y + A B"), "MLIQ size equation enforced: one byte too many is malformed");
            ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { TilesX = 3 }), out problem);
            Check(problem != null && problem.Contains("X = A + 1"), "MLIQ: A other than X - 1 is malformed");
            ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { VerticesX = 0, TilesX = -1 }), out problem);
            Check(problem != null && problem.Contains("X = A + 1"), "MLIQ: negative counts are malformed");
            var huge = new byte[30];
            WmoSynthetic.PutU32(huge, 0, 0x7FFFFFFF); WmoSynthetic.PutU32(huge, 4, 0x7FFFFFFF);
            WmoSynthetic.PutU32(huge, 8, 0x7FFFFFFE); WmoSynthetic.PutU32(huge, 12, 0x7FFFFFFE);
            Check(ParseMliq(huge, out problem) == null && problem.Contains("not 30 + 8 X Y + A B"), "MLIQ: counts near 2^31 fail the size equation without overflow or allocation");
            // X 1825971620, Y 1122491833 (A = X - 1, B = Y - 1) in 102 bytes: 30 + 8 X Y + A B wraps to exactly 102 in 64 bits.
            var wrap = new byte[102];
            WmoSynthetic.PutU32(wrap, 0, 1825971620u); WmoSynthetic.PutU32(wrap, 4, 1122491833u);
            WmoSynthetic.PutU32(wrap, 8, 1825971619u); WmoSynthetic.PutU32(wrap, 12, 1122491832u);
            WmoLiquid wrapped = null;
            string wrapThrew = null;
            problem = null;
            try { wrapped = ParseMliq(wrap, out problem); }
            catch (Exception e) { wrapThrew = e.GetType().Name; }
            Check(wrapThrew == null && wrapped == null && problem != null && problem.Contains("vertices alone need more"),
                  "MLIQ: counts whose size equation wraps to the payload's 102 bytes are malformed, not an exception (" +
                  (wrapThrew ?? problem ?? "accepted") + ")");
            Check(ParseMliq(new byte[29], out problem) == null && problem.Contains("30-byte header"), "MLIQ shorter than its header is malformed");
            Check(WmoLiquid.Parse(new byte[40], 20, 30, out problem) == null && problem.Contains("past the end"), "MLIQ outside the buffer is malformed");
            ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { Corner = new WowVec3(float.NaN, 0, 0) }), out problem);
            Check(problem != null && problem.Contains("corner"), "MLIQ: a non-finite corner is malformed");
            ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { Height = (i, j) => i == 1 && j == 1 ? float.PositiveInfinity : 0f }), out problem);
            Check(problem != null && problem.Contains("(1, 1)"), "MLIQ: a non-finite height at a used vertex is malformed");
            WmoLiquid unused = ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { VerticesX = 4, Height = (i, j) => i == 3 ? float.NaN : 0f, Tile = (i, j) => i == 2 ? (byte)0x0F : (byte)0 }), out problem);
            Check(unused != null && problem == null, "MLIQ: a non-finite height at a vertex no present tile uses is kept");
            WmoLiquid mixed = ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { Tile = (i, j) => (byte)(i == 0 ? 0 : 4) }), out problem);
            Check(mixed != null && mixed.MixedNibbles && mixed.Nibble == 0, "MLIQ: present tiles with two nibble values are flagged");
            WmoLiquid none = ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { VerticesX = 1, VerticesY = 1 }), out problem);
            Check(none != null && none.PresentTiles == 0 && none.Nibble == -1 && none.Tiles.Length == 0, "MLIQ of one vertex: no tiles, not malformed");

            // In a group file: found through MOGP, flag 0x1000 and MLIQ must agree.
            Func<uint, int, WmoGroup> group = (flags, copies) =>
            {
                var spec = new WmoSynthetic.GroupSpec { Flags = WmoGroupFlags.Outdoor | flags };
                for (int k = 0; k < copies; k++)
                    spec.ExtraChunks.Add(new KeyValuePair<string, byte[]>("MLIQ", WmoSynthetic.Mliq(SampleMliq())));
                return WmoParser.ParseGroup(WmoSynthetic.BuildGroup(spec), "synthetic liquid group");
            };
            WmoGroup g = group(WmoGroupFlags.Liquid, 1);
            WmoChunkInfo ci;
            g.TryGetChunk("MLIQ", out ci);
            WmoLiquid gl = WmoLiquid.FromGroup(g, out problem);
            Check(gl != null && problem == null && gl.PresentTiles == 4 && gl.DataOffset == ci.DataOffset, "MLIQ in a group: read from its MOGP sub-chunk");
            Check(WmoLiquid.FromGroup(group(0, 0), out problem) == null && problem == null, "a group without MLIQ or flag 0x1000: no liquid, no problem");
            Check(WmoLiquid.FromGroup(group(WmoGroupFlags.Liquid, 0), out problem) == null && problem.Contains("flag 0x1000 is set"), "flag 0x1000 without MLIQ is reported");
            Check(WmoLiquid.FromGroup(group(0, 1), out problem) == null && problem.Contains("flag 0x1000 is clear"), "MLIQ without flag 0x1000 is reported, not used");
            Check(WmoLiquid.FromGroup(group(WmoGroupFlags.Liquid, 2), out problem) == null && problem.Contains("2 MLIQ"), "two MLIQ chunks are reported");
        }

        static void GroupTypeTests()
        {
            Func<ushort, uint, uint, int, int> t = WmoLiquid.GroupLiquidType;
            Check(t(4, 1, 0, 0) == 13 && t(4, 1, 0x80000, 0) == 14 && t(4, 2, 0, 0) == 14 && t(4, 3, 0, 0) == 19 && t(4, 4, 0, 0) == 20 &&
                  t(4, 5, 0, 0) == 13 && t(4, 7, 0, 0) == 19 && t(4, 20, 0, 0) == 20 && t(0x41F, 21, 0, 0) == 21 && t(4, 1177, 0, 0) == 1177 && t(4, 1344, 0, 4) == 1344,
                  "group type, MOHD 0x4 set: ids below 21 to the WMO rows by (id - 1) & 3 (water 13/14 by MOGP 0x80000, ocean 14, magma 19, slime 20), others as stored");
            Check(t(4, 0, 0, 0) == 0, "group type, MOHD 0x4 set and +0x34 = 0: no type (not the wrapped expression's slime)");
            Check(t(0, 15, 0, 4) == 13 && t(0, 15, 0, 6) == 19 && t(0, 15, 0, 7) == 20 && t(0, 15, 0, 3) == 20 && t(0, 15, 0x80000, 0) == 14 && t(0, 15, 0, -1) == 0,
                  "group type, MOHD 0x4 clear and +0x34 = 15: the tile nibble & 3 gives the kind; no present tile, no type");
            Check(t(0, 1, 0, 0) == 14 && t(0, 2, 0, 0) == 19 && t(0, 0, 0, 0) == 13 && t(0, 20, 0, 0) == 21 && t(0, 25, 0, 0) == 26 && t(4, 0xFFFFFFFF, 0, 0) == 0,
                  "group type, MOHD 0x4 clear, other values: below 20 the kind of the value itself, else value + 1; out of range = 0");
        }

        static void MliqMeshTests()
        {
            string problem;
            WmoLiquid l = ParseMliq(WmoSynthetic.Mliq(SampleMliq()), out problem);
            LiquidSurface s = WmoLiquidMesh.Build(l, 13, Catalog(Type(13, LiquidKind.Water, 0)));
            const double S = 100.0 / 24.0;
            int ok = 0;
            var used = new HashSet<int>();
            for (int v = 0; v < s.VertexCount; v++)
            {
                // Unity x = -wow y, z = wow x: recover (i, j) from the position.
                int i = (int)Math.Round((s.Positions[3 * v + 2] - 10.0) / S), j = (int)Math.Round((-s.Positions[3 * v] - 20.0) / S);
                float ux, uy, uz;
                WowCoordinateConverter.ConvertPosition(new WowVec3((float)(10.0 + i * S), (float)(20.0 + j * S), l.Height(i, j)), out ux, out uy, out uz);
                if (ux == s.Positions[3 * v] && uy == s.Positions[3 * v + 1] && uz == s.Positions[3 * v + 2] &&
                    s.Uv1[2 * v] == l.Depth(i, j) / 255f && s.Uv1[2 * v + 1] == 1f && used.Add(j * 4 + i))
                    ok++;
            }
            int expectUsed = 0;
            for (int j = 0; j < 3; j++)
                for (int i = 0; i < 4; i++)
                    if (l.VertexUsed(i, j)) expectUsed++;
            Check(s.QuadCount == 4 && s.VertexCount == expectUsed && ok == expectUsed && s.Instances == 1 && s.PlanarUvInstances == 1 && s.LiquidType == 13,
                  "MLIQ mesh: present tiles only; vertex (i, j) = corner + (i S, j S, height) through WowCoordinateConverter; uv1 = (depth / 255, 1)");
            Check(MinCrossY(s) > 0 && Math.Abs(MinCrossY(s) - S * S) < 1e-3, "MLIQ mesh winding: Cross.y = S^2 > 0, front faces up");
            Check(s.BoundsMin[1] == -5f && Math.Abs(s.BoundsMax[1] - (-5f + 0.75f + 1f)) < 1e-6, "MLIQ mesh bounds from the liquid's own vertices");

            WmoLiquid st = ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { VerticesX = 4, VerticesY = 3, Word = (i, j) => WmoSynthetic.LiquidStWord((short)(64 * i), (short)(-64 * j)) }), out problem);
            LiquidSurface ss = WmoLiquidMesh.Build(st, 19, Catalog(Type(19, LiquidKind.Magma, 1)));
            bool stOk = ss.StoredUvInstances == 1 && ss.QuadCount == 6;
            for (int v = 0; v < ss.VertexCount; v++)
            {
                int i = (int)Math.Round(ss.Positions[3 * v + 2] / S), j = (int)Math.Round(-ss.Positions[3 * v] / S);
                stOk &= Math.Abs(ss.Uv0[2 * v] - i / 4.0) < 1e-6 && Math.Abs(ss.Uv0[2 * v + 1] - (-j / 4.0)) < 1e-6 && ss.Uv1[2 * v] == 1f && ss.Uv1[2 * v + 1] == 0f;
            }
            Check(stOk, "MLIQ mesh, LVF 1 (magma): uv0 = (s, t) x 1/256 shifted by a whole number, uv1 = (1, 0)");
            Check(WmoLiquidMesh.CarriesSt(Type(1, LiquidKind.Magma, 3)) && WmoLiquidMesh.CarriesSt(Type(1, LiquidKind.Water, 1)) &&
                  !WmoLiquidMesh.CarriesSt(Type(1, LiquidKind.Magma, 0)) && !WmoLiquidMesh.CarriesSt(Type(1, LiquidKind.Slime, 2)) &&
                  WmoLiquidMesh.CarriesSt(Type(1, LiquidKind.Slime, -1)) && !WmoLiquidMesh.CarriesSt(Type(1, LiquidKind.Ocean, -1)) &&
                  !WmoLiquidMesh.CarriesSt(new LiquidTypeView { Id = 1 }) && !WmoLiquidMesh.CarriesSt(null),
                  "MLIQ payload: LVF 1 or 3 = s/t, LVF 0 or 2 = depth; without an LVF the kind (magma, slime = s/t); without either, depth");
            LiquidSurface missing = WmoLiquidMesh.Build(l, 13, null);
            Check(missing.Type == null && missing.QuadCount == 4 && missing.Uv1[1] == 1f, "MLIQ mesh without a catalogue row: the depth reading");
            WmoLiquid absent = ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { Tile = (i, j) => 0x0F }), out problem);
            LiquidSurface empty = WmoLiquidMesh.Build(absent, 13, null);
            Check(empty.VertexCount == 0 && empty.QuadCount == 0 && empty.BoundsMin == null && empty.Instances == 0, "MLIQ without present tiles: an empty surface");
            WmoLiquid planar = ParseMliq(WmoSynthetic.Mliq(new WmoSynthetic.LiquidSpec { VerticesX = 9, VerticesY = 2 }), out problem);
            LiquidSurface ps = WmoLiquidMesh.Build(planar, 13, Catalog(Type(13, LiquidKind.Water, 0, 3f, 0f)));
            float umin = float.PositiveInfinity, umax = float.NegativeInfinity;
            for (int v = 0; v < ps.VertexCount; v++)
                if (ps.Positions[3 * v] == 0f) { umin = Math.Min(umin, ps.Uv0[2 * v + 1]); umax = Math.Max(umax, ps.Uv0[2 * v + 1]); }
            Check(Math.Abs(umax - umin - 3f) < 1e-4, "MLIQ mesh, depth reading: planar uv0 in the group frame, Float0 = 3 repeats over 8 tiles (a chunk) north");
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("MH2O decode: formats, layout");
            FormatTests();
            log.Add("MH2O decode: exists bitmaps, partial instances");
            BitmapTests();
            log.Add("MH2O decode: layers and attributes");
            LayerAndAttributeTests();
            log.Add("MH2O decode: malformed structures");
            MalformedTests();
            log.Add("Format rule");
            FormatRuleTests();
            log.Add("Terrain liquid mesh");
            TerrainMeshTests();
            log.Add("MLIQ parse");
            MliqParseTests();
            log.Add("Group liquid type");
            GroupTypeTests();
            log.Add("MLIQ mesh");
            MliqMeshTests();
            log.Add(string.Format("LiquidTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
