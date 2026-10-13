// TerrainMaterialTests.cs
//
// Unit tests of the terrain material data (Assets/Scripts/Wow/World/TerrainMaterial.cs) and the BLP
// header API (BlpDecoder.ReadHeader) and exact-blocks decode on synthetic bytes from AdtSynthetic: no game
// content, no file IO. Framework-free like TerrainMeshTests: RunAll returns the failure count. The same
// builder on real client bytes is TerrainMaterialCorpusTests.cs.
//
// What is proven here (stage 04 design, sections 1 and 3, with the v3 delta, sections 1, 2 and 5, and the
// round-3 rules): every chunk state alone and the precedence over every combination of the layer conditions
// (before and after height-texture failures); 1..8 layers drawn and 9 (appended to a parsed chunk: AdtParser
// rejects more than 8) as layerCountUnsupported; RLE, raw 8-bit and raw 4-bit storage; alpha sums of exactly
// 255 and above it; the MPHD 0x84 cross-check; MCCV as B, G, R / 127, the flag-0x40 rule and no vertex colours
// for neutral chunks; texture scales 0..4 (8 / 2^s repeats as a float, the chunk's UV phase (frac(ix * r),
// frac(iy * r)) on every chunk); a scale nibble above 4 and MTXP height values outside the height mix's domain
// (H below 0 or a float32 overflow on an MPHD 0x80 map) as layerEffectUnresolved, with their precedence and the
// domain's boundaries; the effect rule (MCLY and MTXP bits); the MDID/MHID/MTXP table rules (repeats,
// unreadable and short tables); missing textures (id 0 and failed reads) and failed height textures; the
// diffuse and height texture ids in first use with their reference counts, the plain-only ids without layers
// of unresolved repeats, and the slice numbering (a second slice for chunks of 6..8 layers); the alpha slice
// layout and orientation; the compositor against hand-computed values (alpha bilinear at the texel centre
// alignment with clamp, layers 5..7 from the second slice, the height-mix cutoff with H on and off by MPHD
// 0x80, 6..8 layers, ties at the maximum, S == 0, S < 0 at the weights function only (such chunks are not
// drawn), MHID 0 on a 0x80 map, a single layer, the height texture's alpha channel at the layer's coordinate,
// nibble 4 at even and odd ix and iy, plain weights as the labelled alternative, repeats, texel indices, the
// missing-texture checker and the checker for a layer of unresolved repeats, the MCCV fan, holes, grid
// orientation); the BLP header and mip table; BlpDecoder's opt-in exact-blocks DXT decode against hand
// values, with the default decode pinned unchanged.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Wmv.Wow;
using Wmv.Wow.World;
using static Wmv.Wow.World.AdtSynthetic;

namespace Wmv.Wow.Tests
{
    public static class TerrainMaterialTests
    {
        const int TX = 32, TY = 50;
        const uint Mphd8 = 0x3CA;    // Retail/Beta Azeroth: 0x80 set -> 8-bit, height textures enter the mix
        const uint Mphd8NoHeight = 0x24C;   // 0x4 set, 0x80 clear -> 8-bit, H = 1
        const uint Mphd4 = 0x248;    // Era Azeroth: neither 0x4 nor 0x80 -> 4-bit

        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name, string detail = null)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name + (detail != null ? " (" + detail + ")" : "")); failures++; }
        }

        static void Throws<TEx>(Action action, string name, string mustContain = null) where TEx : Exception
        {
            try
            {
                action();
                log.Add("  FAIL  " + name + " (expected " + typeof(TEx).Name + ", nothing was thrown)");
                failures++;
            }
            catch (TEx e)
            {
                if (mustContain != null && e.Message.IndexOf(mustContain, StringComparison.Ordinal) < 0)
                {
                    log.Add("  FAIL  " + name + " (message lacks '" + mustContain + "': " + e.Message + ")");
                    failures++;
                }
                else
                    log.Add("  PASS  " + name);
            }
            catch (Exception e)
            {
                log.Add("  FAIL  " + name + " (expected " + typeof(TEx).Name + ", got " + e.GetType().Name + ": " + e.Message + ")");
                failures++;
            }
        }

        static bool Near(float a, double b, double tol = 1e-6) { return Math.Abs(a - b) <= tol; }

        // ================================================================================== helpers

        /// <summary>Root flags of a well-formed chunk: 0x10000 (64-bit hole mask) and 0x8000 (as every audited chunk).</summary>
        const uint RootFlags = 0x18000;

        static RootChunkSpec Rs(uint flags = RootFlags) { return new RootChunkSpec { Flags = flags }; }

        static TerrainTileMaterial BuildTile(Tex0Spec tex, uint mphd = Mphd8, Func<int, RootChunkSpec> root = null, Action<AdtTex> edit = null)
        {
            AdtRoot r = AdtParser.ParseRoot(RootWith(TX, TY, root ?? (n => Rs())), TX, TY);
            AdtTex t = AdtParser.ParseTex(Tex0(tex));
            if (edit != null)
                edit(t);
            return TerrainMaterialBuilder.Build(r, t, mphd);
        }

        /// <summary>Append one RLE layer naming MDID entry `textureIndex` with `alpha` to a PARSED tex0 chunk: the only
        /// way to give the builder more than 8 layers, since AdtParser rejects more than 8 MCLY entries (Malformed).</summary>
        static void AppendLayer(AdtTexChunk tc, uint textureIndex, byte[] alpha)
        {
            var layers = tc.Layers.ToList();
            layers.Add(new TerrainLayer { TextureIndex = textureIndex, Flags = 0x300 });
            var maps = (tc.Alpha ?? new AlphaMap[0]).ToList();
            while (maps.Count < layers.Count - 1) maps.Add(null);
            maps.Add(new AlphaMap { Storage = AlphaStorage.Compressed, Samples = (byte[])alpha.Clone(), StoredBytes = 0 });
            tc.Layers = layers.ToArray();
            tc.Alpha = maps.ToArray();
        }

        /// <summary>An asymmetric alpha pattern for layer k (0..49) whose sum over 5 alpha layers stays below 255.</summary>
        static byte[] Pattern(int k)
        {
            return AlphaMapOf((r, c) => (r * 3 + c * 5 + 17 * k) % 50);
        }

        static byte[] Constant(int value) { return AlphaMapOf((r, c) => value); }

        static TexChunkSpec Empty() { return new TexChunkSpec(); }

        static string States(TerrainTileMaterial t, params int[] chunks)
        {
            return string.Join(", ", chunks.Select(n => n + ":" + TerrainTileMaterial.StateName(t.Chunks[n].State)));
        }

        static string Counters(TerrainTileMaterial t)
        {
            return string.Join(", ", t.CounterPairs().Where(p => p.Value > 0).Select(p => p.Key + " " + p.Value));
        }

        // ================================================================================== alignment and encoders

        static void AlignmentTests()
        {
            Check(TerrainMaterialBuilder.AlphaTexelCentre(0) == 0.5 / 64 && TerrainMaterialBuilder.AlphaTexelCentre(63) == 63.5 / 64 &&
                  TerrainMaterialBuilder.AlphaTexelCentre(32) == 0.5078125, "AlphaTexelCentre(i) = (i + 0.5) / 64");
            bool inverse = true;
            for (int i = 0; i < 64; i++)
                inverse &= TerrainMaterialBuilder.AlphaTexelCoordinate((float)TerrainMaterialBuilder.AlphaTexelCentre(i)) == i;
            Check(inverse && TerrainMaterialBuilder.AlphaTexelCoordinate(0f) == -0.5f && TerrainMaterialBuilder.AlphaTexelCoordinate(1f) == 63.5f,
                  "AlphaTexelCoordinate is the inverse of AlphaTexelCentre: texel i at exactly i, the chunk edges at -0.5 and 63.5");
            float u, v;
            var uv = new List<string>();
            bool ok = true;
            foreach (var e in new[] { new[] { 0, 0, 0 }, new[] { 8, 16, 0 }, new[] { 9, 1, 1 }, new[] { 144, 16, 16 }, new[] { 135, 15, 15 }, new[] { 17, 0, 2 }, new[] { 25, 16, 2 } })
            {
                TerrainMaterialBuilder.VertexUv(e[0], out u, out v);
                ok &= u == e[1] / 2f && v == e[2] / 2f;
                uv.Add(e[0] + "->(" + u + "," + v + ")");
            }
            Check(ok, "VertexUv: outer (r, c) -> (c, r), inner -> (c + 0.5, r + 0.5)", string.Join(" ", uv));
            Throws<ArgumentOutOfRangeException>(() => TerrainMaterialBuilder.VertexUv(145, out u, out v), "VertexUv rejects k = 145");

            var rng = new Random(4);
            bool rle = true, raw4 = true, rows = true;
            for (int trial = 0; trial < 40; trial++)
            {
                var values = new byte[4096];
                int mode = trial % 4;
                for (int i = 0; i < 4096; i++)
                    values[i] = mode == 0 ? (byte)rng.Next(256) : mode == 1 ? (byte)(rng.Next(4) * 60) : mode == 2 ? (byte)((i / 64) * 4) : (byte)(i % 7 == 0 ? 9 : 0);
                byte[] enc = RleEncode(values);
                string err;
                AlphaMap m = AdtParser.DecodeRle(enc, 0, enc.Length, out err);
                rle &= m != null && m.Samples.SequenceEqual(values) && m.StoredBytes == enc.Length;
                int p = 0, filled = 0;
                while (p < enc.Length)
                {
                    int n = enc[p] & 0x7F;
                    rows &= n > 0 && filled / 64 == (filled + n - 1) / 64;
                    filled += n;
                    p += (enc[p] & 0x80) != 0 ? 2 : 1 + n;
                }
                var nib = new byte[4096];
                for (int i = 0; i < 4096; i++) nib[i] = (byte)(values[i] & 15);
                byte[] packed = Raw4(nib);
                AlphaMap m4 = AdtParser.DecodeRaw(packed, 0, packed.Length, 0, packed.Length, out err);
                raw4 &= m4 != null && m4.Storage == AlphaStorage.Uncompressed4 && m4.Samples.SequenceEqual(nib) && (packed[0] & 15) == nib[0];
            }
            Check(rle, "AdtSynthetic.RleEncode: 40 maps decode back exactly through AdtParser.DecodeRle");
            Check(rows, "AdtSynthetic.RleEncode: no run is empty or crosses a 64-texel row");
            Check(raw4, "AdtSynthetic.Raw4: 2048 bytes, low nibble first, decode back exactly");

            byte[] tex = Tex0(new Tex0Spec { Mdid = new uint[] { 7, 8 }, Mtxp = Cat(MtxpEntry(0), MtxpEntry(0x10)) });
            AdtTex parsed = AdtParser.ParseTex(tex);
            Check(string.Join(" ", parsed.Chunks.Select(c => c.Tag).Where(s => s != "MCNK")) == "MVER MAMP MDID MHID MTXP" &&
                  parsed.Chunks.Count(c => c.Tag == "MCNK") == 256 && parsed.MtxpRaw.Length == 32 && parsed.Issues.Count == 0 &&
                  parsed.HeightTextureIds.Length == 2 && parsed.MampRaw.Length == 4,
                  "AdtSynthetic.Tex0: MVER MAMP MDID MHID, 256 MCNKs, MTXP; parses without issues",
                  string.Join(" ", parsed.Chunks.Select(c => c.Tag).Distinct()) + "; issues " + parsed.Issues.Count);
            AdtTex extra = AdtParser.ParseTex(Tex0(new Tex0Spec { MhidRaw = new byte[6], Extra = new[] { Chunk("MDID", U32Payload(5, 6)) } }));
            Check(extra.HeightTextureIds.Length == 0 && extra.Issues.Count(i => i.Kind == WorldIssueKind.Malformed && i.Chunk == "MHID") == 1 &&
                  extra.Issues.Count(i => i.Kind == WorldIssueKind.Malformed && i.Chunk == "MDID") == 1 && extra.DiffuseTextureIds[0] == 1000,
                  "AdtSynthetic.Tex0: a raw MHID payload and extra chunks written last (the parser reports both, the first MDID is used)",
                  string.Join(" | ", extra.Issues.Select(i => i.Chunk + ": " + i.Reason)));
        }

        // ================================================================================== states one by one

        static readonly uint[] StateMdid = { 1000, 1001, 1002, 1003, 1004, 0, 1006, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 1015 };
        static readonly uint[] StateMhid = { 0, 0, 0, 0, 2004, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

        static byte[] StateMtxp()
        {
            var e = new List<byte[]>();
            for (int i = 0; i < StateMdid.Length; i++)
                e.Add(i == 6 ? MtxpEntry(0, 5f, 1f) : i == 7 ? MtxpEntry(0x40) : i == 8 ? MtxpEntry(0, 0f, 0.5f) : i == 9 ? MtxpEntry(0x30)
                    : i == 10 ? MtxpEntry(0x1) : i == 11 ? MtxpEntry(0x100) : i == 12 ? MtxpEntry(0, 0f, 1f, 7) : i == 13 ? MtxpEntry(0, float.NaN, 1f)
                    : i == 14 ? MtxpEntry(0x11) : i == 15 ? MtxpEntry(0x50) : MtxpEntry(0));
            return Cat(e.ToArray());
        }

        const int FirstDefaultChunk = 44;

        static TexChunkSpec StateChunk(int n)
        {
            switch (n)
            {
                case 0: return TexChunkSpec.Of(Layer(0));
                case 1: return TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Compressed, Pattern(1)));
                case 2: return TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Uncompressed8, Pattern(1)), Layer(2, AlphaStorage.Uncompressed8, Pattern(2)));
                case 3: return TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Uncompressed8, Pattern(1)), Layer(2, AlphaStorage.Compressed, Pattern(2)), Layer(3, AlphaStorage.Compressed, Pattern(3)));
                case 4: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)), Layer(2, alpha: Pattern(2)), Layer(3, alpha: Pattern(3)), Layer(9, alpha: Pattern(4)));
                case 5: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)), Layer(2, alpha: Pattern(2)), Layer(3, alpha: Pattern(3)), Layer(9, alpha: Pattern(4)), Layer(1, alpha: Pattern(5)));
                case 6: return Empty();
                case 7: return new TexChunkSpec { OmitMcly = true };
                case 8: return TexChunkSpec.Of(Layer(0), Layer(5, alpha: Pattern(1)));
                case 9: return TexChunkSpec.Of(Layer(5), Layer(1, alpha: Pattern(1)));
                case 10: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1), extraFlags: 0x40));
                case 11: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1), extraFlags: 0x80));
                case 12: return TexChunkSpec.Of(Layer(0, extraFlags: 0x400), Layer(1, alpha: Pattern(1)));
                case 13: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1), extraFlags: 0x3));
                case 14: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1), extraFlags: 0x800));
                case 15: return TexChunkSpec.Of(Layer(0), Layer(4, alpha: Pattern(1)));
                case 16: return TexChunkSpec.Of(Layer(0), Layer(6, alpha: Pattern(1)));
                case 17: return TexChunkSpec.Of(Layer(0), Layer(8, alpha: Pattern(1)));
                case 18: return TexChunkSpec.Of(Layer(0), Layer(7, alpha: Pattern(1)));
                case 19: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: AlphaMapOf((r, c) => r == 5 && c == 9 ? 129 : 100)), Layer(2, AlphaStorage.Uncompressed8, Constant(127)));
                case 20: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: AlphaMapOf((r, c) => (r * 64 + c) % 256)), Layer(2, AlphaStorage.Uncompressed8, AlphaMapOf((r, c) => 255 - (r * 64 + c) % 256)));
                case 21: return TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Uncompressed4, AlphaMapOf((r, c) => c % 16)));
                case 22: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)));
                case 23: return TexChunkSpec.Of(Layer(0), Layer(20, alpha: Pattern(1)));
                case 24:
                {
                    TexChunkSpec s = TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)), Layer(2, alpha: Pattern(2)));
                    s.Layers[2].AlphaOffset = 3;
                    return s;
                }
                case 25:
                {
                    TexChunkSpec s = TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)));
                    s.Layers[1].Flags = 0x200;
                    return s;
                }
                case 26: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)));
                case 27:
                {
                    TexChunkSpec s = TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)));
                    s.Shadow = new byte[512];
                    return s;
                }
                case 31: return TexChunkSpec.Of(Layer(0), Layer(9, alpha: Pattern(1)));
                case 32: return TexChunkSpec.Of(Layer(0, extraFlags: 0x1000), Layer(1, alpha: Pattern(1)));
                case 33: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1), extraFlags: 0x38));
                case 34: return TexChunkSpec.Of(Layer(0), Layer(10, alpha: Pattern(1)));
                case 35: return TexChunkSpec.Of(Layer(0), Layer(11, alpha: Pattern(1)));
                case 36: return TexChunkSpec.Of(Layer(0), Layer(12, alpha: Pattern(1)));
                case 37: return TexChunkSpec.Of(Layer(0), Layer(13, alpha: Pattern(1)));
                case 38: return TexChunkSpec.Of(Layer(0), Layer(14, alpha: Pattern(1)));
                case 39: return TexChunkSpec.Of(Layer(0, extraFlags: 0x40));
                case 40: return TexChunkSpec.Of(Layer(5), Layer(1, alpha: Pattern(1), extraFlags: 0x80));
                case 41: return Empty();
                case 42: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)));
                case 43: return TexChunkSpec.Of(Layer(0), Layer(15, alpha: Pattern(1)));
                default: return TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)));
            }
        }

        static RootChunkSpec StateRoot(int n)
        {
            switch (n)
            {
                case 22: return Rs(0x10000);
                case 26: return new RootChunkSpec { Flags = RootFlags, VertexColorBytes = new byte[100] };
                case 28: return new RootChunkSpec { Flags = RootFlags, VertexColorBytes = MccvBytes() };
                case 29: return new RootChunkSpec { Flags = RootFlags, VertexColorBytes = MccvBytes(), VertexColorFlag = false };
                case 30: return Rs(RootFlags | 0x40);
                case 41: return new RootChunkSpec { Flags = RootFlags, VertexColorBytes = MccvBytes() };
                case 42: return new RootChunkSpec { Flags = 0x10000, VertexColorBytes = MccvBytes() };
                default: return Rs();
            }
        }

        /// <summary>145 x (B, G, R, A): B = v, G = 127, R = 254 - v, A = 255.</summary>
        static byte[] MccvBytes()
        {
            var b = new byte[580];
            for (int v = 0; v < 145; v++) { b[4 * v] = (byte)v; b[4 * v + 1] = 127; b[4 * v + 2] = (byte)(254 - v); b[4 * v + 3] = 255; }
            return b;
        }

        static void StateTests()
        {
            TerrainTileMaterial t = BuildTile(new Tex0Spec { Mdid = StateMdid, Mhid = StateMhid, Mtxp = StateMtxp(), Chunk = StateChunk }, Mphd8, StateRoot);
            TerrainMaterialState Tx = TerrainMaterialState.Textured, Stat = TerrainMaterialState.LayerEffectStatic, Unres = TerrainMaterialState.LayerEffectUnresolved;
            // chunk 5 (6 layers) and chunk 18 (MTXP scale nibble 4) are drawn since round 3 of the stage (6..8 layers and
            // nibble 4 verified on the game's map texture of Retail map 2923); chunk 43 holds the unresolved nibble 5
            var expect = new Dictionary<int, TerrainMaterialState>
            {
                { 0, Tx }, { 1, Tx }, { 2, Tx }, { 3, Tx }, { 4, Tx }, { 5, Tx },
                { 6, TerrainMaterialState.NoLayers }, { 7, TerrainMaterialState.NoLayers },
                { 8, TerrainMaterialState.MissingTexture }, { 9, TerrainMaterialState.MissingTexture },
                { 10, Stat }, { 11, Stat }, { 12, Unres }, { 13, Stat }, { 14, Unres },
                { 15, Tx }, { 16, Tx }, { 17, Tx },
                { 18, Tx }, { 19, TerrainMaterialState.Malformed }, { 20, Tx },
                { 21, TerrainMaterialState.Malformed }, { 22, TerrainMaterialState.AlphaUnsupported },
                { 23, TerrainMaterialState.Malformed }, { 24, TerrainMaterialState.Malformed }, { 25, TerrainMaterialState.Malformed },
                { 26, TerrainMaterialState.Malformed }, { 27, Tx }, { 28, Tx }, { 29, Tx }, { 30, Tx }, { 31, Tx },
                { 32, Unres }, { 33, Stat }, { 34, Unres }, { 35, Unres }, { 36, Unres }, { 37, TerrainMaterialState.Malformed }, { 38, Unres },
                { 39, Stat }, { 40, Stat }, { 41, TerrainMaterialState.NoLayers }, { 42, TerrainMaterialState.AlphaUnsupported }, { 43, Unres },
            };
            string[] names =
            {
                "1 layer", "2 layers RLE", "3 layers raw 8-bit", "4 layers raw + RLE", "5 layers", "6 layers", "empty MCLY", "no MCLY",
                "MDID 0 on an alpha layer", "MDID 0 on the base layer", "MCLY 0x40 (animation)", "MCLY 0x80 (overbright)", "MCLY 0x400 (base layer)",
                "rotation bits 0x3 alone", "never-observed bit 0x800", "MHID != 0 on an 0x80 map", "MTXP height scale 5.0", "MTXP height offset 0.5",
                "MTXP scale nibble 4", "alpha sum 256 at one texel", "alpha sum exactly 255 everywhere", "4-bit layer on an 8-bit map", "MCNK flag 0x8000 clear",
                "texture index outside MDID", "alpha offsets inconsistent", "flag 0x100 missing on layer 1", "root MCNK malformed (MCCV of 100 bytes)",
                "MCSH present", "MCCV with flag 0x40", "MCCV without flag 0x40", "flag 0x40 without MCCV", "MTXP scale nibble 3",
                "never-observed MCLY bit 0x1000 (base layer)", "speed bits 0x38 alone", "MTXP flags 0x1 (cube-map texture)", "MTXP flag 0x100 (never observed)",
                "MTXP word 3 = 7", "MTXP height scale NaN", "MTXP flags 0x11 (scale 1 + cube-map texture)", "single layer with MCLY 0x40",
                "MDID 0 base layer + MCLY 0x80 layer", "no layer, MCCV with flag 0x40", "flag 0x8000 clear, MCCV with flag 0x40", "MTXP scale nibble 5",
            };
            foreach (var e in expect)
            {
                TerrainChunkMaterial c = t.Chunks[e.Key];
                Check(c.State == e.Value && (c.State == Tx) == (c.Reason == null), "state, chunk " + e.Key + " (" + names[e.Key] + "): " + TerrainTileMaterial.StateName(e.Value),
                      TerrainTileMaterial.StateName(c.State) + ": " + c.Reason);
            }
            Check(t.Chunks.Skip(FirstDefaultChunk).All(c => c.State == Tx), "state: the " + (256 - FirstDefaultChunk) + " default chunks (2 layers) are textured");
            Check(t.Chunks[19].Reason.Contains("sum to 256 at texel (row 5, column 9)"), "reason names the texel whose alpha sum exceeds 255", t.Chunks[19].Reason);
            Check(t.Chunks[21].Reason.Contains("4-bit") && t.Chunks[21].Reason.Contains("0x3CA"), "reason: MPHD 0x84 vs the stored bit depth", t.Chunks[21].Reason);
            TerrainMaterialLayer n4 = t.Chunks[18].Layers[1], n5 = t.Chunks[43].Layers[1];
            Check(n4.ScaleExponent == 4 && n4.RepeatsPerChunk == 0.5f && n4.PhaseU == 0f && n4.PhaseV == 0.5f && t.Chunks[18].Drawable &&
                  t.Chunks[18].Alpha != null && t.Chunks[18].Layers[0].RepeatsPerChunk == 8f && t.Chunks[18].Layers[0].PhaseV == 0f,
                  "MTXP scale nibble 4: textured at 0.5 repeats; chunk 18 (ix 2, iy 1) adds the phase (frac(2 * 0.5), frac(1 * 0.5)) = (0, 0.5)",
                  n4.RepeatsPerChunk + " (" + n4.PhaseU + ", " + n4.PhaseV + ")");
            Check(t.Chunks[43].Reason == "layer 1 (texture 1015): MTXP entry 15 has flags 0x50: MTXP texture scale nibble 5: never observed (0..4 verified)" &&
                  n5.ScaleExponent == 5 && n5.RepeatsPerChunk == 0f && n5.PhaseU == 0f && n5.PhaseV == 0f && t.Chunks[43].PlainDiagnostic && t.Chunks[43].Alpha != null,
                  "MTXP scale nibble 5: layerEffectUnresolved (not malformed: never observed), named; repeats 0, no phase; plain-diagnostic slice",
                  t.Chunks[43].Reason);
            Check(t.Chunks[5].Layers.Length == 6 && t.Chunks[5].AlphaSliceCount == 2 && t.Chunks[5].AlphaSlice2 == t.Chunks[5].AlphaSlice + 1 &&
                  t.Chunks[4].AlphaSliceCount == 1 && t.Chunks[4].AlphaSlice2 == -1 && t.Chunks[8].AlphaSlice == t.Chunks[5].AlphaSlice + 2,
                  "6 layers: drawn, with a second alpha slice right after its first (the next drawable chunk, 8, follows it); 5 layers: one slice",
                  t.Chunks[5].AlphaSlice + " / " + t.Chunks[5].AlphaSlice2 + ", next " + t.Chunks[8].AlphaSlice);
            Check(t.Chunks[23].Reason.Contains("names texture 20, but MDID has 16"), "reason: the parser's issue is quoted for a Malformed tex0 MCNK", t.Chunks[23].Reason);
            Check(t.Chunks[26].Reason.StartsWith("root MCNK 26 is malformed", StringComparison.Ordinal) && t.Chunks[26].Reason.Contains("580"),
                  "reason: a Malformed root MCNK", t.Chunks[26].Reason);
            Check(t.Chunks[14].Reason.Contains("never observed 0x800") && t.Chunks[32].Reason.Contains("never observed 0x1000") &&
                  t.Chunks[12].Reason.Contains("cube map 0x400"), "reason: the unresolved MCLY bits are named",
                  t.Chunks[14].Reason + " | " + t.Chunks[32].Reason + " | " + t.Chunks[12].Reason);
            Check(t.Chunks[13].Reason.Contains("rotation 0x3") && t.Chunks[33].Reason.Contains("speed 0x38") && t.Chunks[10].Reason.Contains("animation 0x40") &&
                  t.Chunks[11].Reason.Contains("overbright 0x80") && t.Chunks[10].Reason.Contains("static composite") && t.Chunks[10].Reason.Contains("not applied"),
                  "reason: the static effect bits are named, drawn with the static composite, effects not applied", t.Chunks[10].Reason);
            Check(t.Chunks[34].Reason.Contains("0x1 = cube-map texture") && t.Chunks[38].Reason.Contains("0x1 = cube-map texture") &&
                  t.Chunks[35].Reason.Contains("never observed 0x100") && t.Chunks[36].Reason.Contains("word 3 is 0x7"),
                  "reason: MTXP flag bits outside 0xF0 and word 3 are named", t.Chunks[34].Reason + " | " + t.Chunks[35].Reason + " | " + t.Chunks[36].Reason);
            Check(t.Chunks[37].Reason.Contains("not finite"), "reason: a non-finite MTXP height value is malformed", t.Chunks[37].Reason);
            Check(t.Chunks[40].Reason.Contains("overbright 0x80") && t.Chunks[40].Reason.Contains("texture id 0") && t.Chunks[40].Layers[0].Missing &&
                  t.Chunks[40].Drawable, "layerEffectStatic over missingTexture: the id-0 layer is still the checker and named", t.Chunks[40].Reason);

            // counters
            int malformed = expect.Count(e => e.Value == TerrainMaterialState.Malformed);
            int stat = expect.Count(e => e.Value == Stat), unres = expect.Count(e => e.Value == Unres);
            Check(t.Count(TerrainMaterialState.Malformed) == malformed && t.Count(TerrainMaterialState.AlphaUnsupported) == 2 &&
                  t.Count(TerrainMaterialState.LayerCountUnsupported) == 0 && t.Count(Unres) == unres && unres == 8 &&
                  t.Count(TerrainMaterialState.HeightTextureMissing) == 0 && t.Count(TerrainMaterialState.NoLayers) == 3 && t.Count(Stat) == stat && stat == 6 &&
                  t.Count(TerrainMaterialState.MissingTexture) == 2 && t.Count(Tx) == 256 - malformed - 2 - unres - 3 - stat - 2,
                  "counters: one per chunk, by state (nine states)", Counters(t));
            Check(t.CounterPairs().Select(p => p.Key).Take(9).SequenceEqual(new[] { "malformed", "alphaUnsupported", "layerCountUnsupported", "layerEffectUnresolved",
                  "heightTextureMissing", "noLayers", "layerEffectStatic", "missingTexture", "textured" }), "counter names in precedence order");
            Check(t.State == TerrainTileMaterialState.Partial && t.Reason.Contains("malformed " + malformed) && t.Reason.Contains("layerEffectStatic " + stat),
                  "tile state partial, reason lists the counters", t.Reason);
            Check(t.ShadowMapsIgnored == 1 && t.Chunks[27].ShadowMapIgnored && t.VertexColorMismatches == 2 && t.Chunks[29].VertexColorMismatch &&
                  t.Chunks[30].VertexColorMismatch && !t.Chunks[28].VertexColorMismatch, "notes: MCSH counted, MCCV/flag 0x40 disagreements counted (not states)");

            // layers, repeats, storage, height values
            TerrainChunkMaterial c4 = t.Chunks[4];
            Check(c4.Layers.Length == 5 && c4.Layers.Select(l => l.TextureFileDataID).SequenceEqual(new uint[] { 1000, 1001, 1002, 1003, 1009 }) &&
                  c4.Layers.Select(l => l.MdidIndex).SequenceEqual(new[] { 0, 1, 2, 3, 9 }) && c4.Layers[0].Storage == null &&
                  c4.Layers.Skip(1).All(l => l.Storage == AlphaStorage.Compressed && l.Flags == 0x300),
                  "layers: MDID ids, indices, flags and storage in MCLY order");
            Check(t.Chunks[31].Layers[1].RepeatsPerChunk == 1 && t.Chunks[31].Layers[1].ScaleExponent == 3 && t.Chunks[31].Layers[0].RepeatsPerChunk == 8 &&
                  t.Chunks[2].Layers.All(l => l.Storage == null || l.Storage == AlphaStorage.Uncompressed8), "repeats: MTXP nibble 3 -> 1, nibble 0 -> 8; raw 8-bit storage");
            // RLE layers: 32 in chunks 0..31 (as before the effect cases), 10 in chunks 32..43, one per default chunk
            Check(t.Raw4Layers == 1 && t.Raw8Layers == 5 && t.RleLayers == 32 + 10 + (256 - FirstDefaultChunk),
                  "storage counters: every decoded alpha layer by storage", "raw8 " + t.Raw8Layers + ", rle " + t.RleLayers + ", raw4 " + t.Raw4Layers);
            Check(t.UseHeightTextures && t.Chunks[15].Layers[1].HeightTextureFileDataID == 2004 && t.Chunks[15].NeedsHeightTextures &&
                  t.Chunks[16].Layers[1].HeightScale == 5f && t.Chunks[16].Layers[1].HeightOffset == 1f && t.Chunks[17].Layers[1].HeightOffset == 0.5f &&
                  t.Chunks[1].Layers.All(l => l.HeightScale == 0f && l.HeightOffset == 1f && l.HeightTextureFileDataID == 0) && !t.Chunks[1].NeedsHeightTextures &&
                  t.Chunks[36].Layers[1].MtxpWord3 == 7,
                  "height values: MHID id, MTXP height scale/offset (defaults 0.0/1.0), word 3, carried per layer on an MPHD 0x80 map");
            Check(t.HeightTextureIds.SequenceEqual(new uint[] { 2004 }) && t.HeightLayerReferences == 1 && t.HeightDuplicateReferences == 0,
                  "height texture ids of the drawable chunks: [2004]", string.Join(" ", t.HeightTextureIds));

            // alpha slices, every texel: layer k (1..7) in slice (k - 1) / 4, channel (k - 1) % 4
            bool slice = true;
            AdtTex parsed = AdtParser.ParseTex(Tex0(new Tex0Spec { Mdid = StateMdid, Mhid = StateMhid, Mtxp = StateMtxp(), Chunk = StateChunk }));
            foreach (int n in new[] { 1, 2, 3, 4, 5, 20, 31 })
            {
                TerrainChunkMaterial c = t.Chunks[n];
                slice &= c.Alpha.Length == (c.Layers.Length > 5 ? 2 : 1) * 16384;
                for (int i = 0; i < 4096; i++)
                    for (int k = 1; k <= 4 * c.AlphaSliceCount; k++)
                    {
                        byte want = k < c.Layers.Length ? parsed.Mcnk[n].Alpha[k].Samples[i] : (byte)0;
                        slice &= c.Alpha[16384 * ((k - 1) / 4) + 4 * i + (k - 1) % 4] == want;
                    }
            }
            Check(slice, "alpha slices: layer k at 16384 * ((k - 1) / 4) + 4 * (row*64 + col) + (k - 1) % 4, unused channels 0 (2, 3, 4, 5 and 6 layers; a second slice for 6)");
            Check(t.Chunks[4].Alpha[4 * (0 * 64 + 63) + 0] == Pattern(1)[63] && t.Chunks[4].Alpha[4 * (63 * 64 + 0) + 3] == Pattern(4)[63 * 64] &&
                  Pattern(1)[63] != Pattern(1)[63 * 64], "alpha slices: row 0 is the first stored row (north), never flipped");
            Check(t.Chunks[0].Alpha != null && t.Chunks[0].Alpha.All(b => b == 0) && t.Chunks[39].Alpha != null, "a single-layer chunk has an all-zero slice (w0 = 1)");
            Check(t.Chunks.Where(c => !c.Drawable && !c.PlainDiagnostic).All(c => c.Alpha == null && c.AlphaSlice == -1),
                  "chunks drawn neutral have no slice");
            Check(t.Chunks.Where(c => c.PlainDiagnostic).All(c => c.Alpha != null && c.AlphaSlice >= t.DrawableSlices) && t.PlainSlices == unres &&
                  t.DrawableSlices == t.DrawableChunks + 1, "layerEffectUnresolved chunks have slices after the drawable ones (layerEffectStatic chunks are " +
                  "drawable; the 6-layer chunk has two)", t.DrawableSlices + " drawable slices for " + t.DrawableChunks + " chunks, " + t.PlainSlices + " plain");

            // MCCV
            float[] col = t.Chunks[28].VertexColors;
            bool mccv = col != null && col.Length == 435;
            for (int v = 0; mccv && v < 145; v++)
                mccv &= col[3 * v] == (254 - v) / 127f && col[3 * v + 1] == 1f && col[3 * v + 2] == v / 127f;
            Check(mccv, "MCCV: R, G, B = stored bytes 2, 1, 0 divided by 127; 127 is exactly 1.0");
            Check(t.Chunks[29].VertexColors == null && t.Chunks[30].VertexColors == null && t.Chunks[1].VertexColors == null && t.VertexColorChunks == 1,
                  "MCCV applied only when flag 0x40 and MCCV are both present");
            Check(t.Chunks[41].VertexColors == null && t.Chunks[42].VertexColors == null && !t.Chunks[41].VertexColorMismatch,
                  "no vertex colours for chunks drawn neutral (noLayers, alphaUnsupported) even with MCCV and flag 0x40");
        }

        static void FourBitAndTileTests()
        {
            // 4-bit map: 4-bit layers are AlphaUnsupported, 8-bit layers disagree with MPHD, single layers draw.
            TerrainTileMaterial t = BuildTile(new Tex0Spec
            {
                Mdid = new uint[] { 1000, 1001 },
                Chunk = n => n == 0 ? TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Uncompressed4, AlphaMapOf((r, c) => (r + c) % 16)))
                           : n == 1 ? TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Compressed, Pattern(1)))
                           : n == 2 ? TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Uncompressed8, Pattern(1)))
                           : n == 3 ? TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Uncompressed4, Constant(15)), Layer(0, AlphaStorage.Uncompressed4, Constant(15)))
                           : TexChunkSpec.Of(Layer(1)),
            }, Mphd4);
            Check(t.AlphaBitDepth == 4 && t.AlphaFormat == "4-bit" && t.Chunks[0].State == TerrainMaterialState.AlphaUnsupported &&
                  t.Chunks[0].Reason.Contains("4-bit") && t.Chunks[0].Alpha == null,
                  "4-bit map: a 4-bit layer is alphaUnsupported (no slice, never expanded)", States(t, 0, 1, 2, 3, 4) + "; " + t.Chunks[0].Reason);
            Check(t.Chunks[1].State == TerrainMaterialState.Malformed && t.Chunks[2].State == TerrainMaterialState.Malformed &&
                  t.Chunks[1].Reason.Contains("RLE") && t.Chunks[2].Reason.Contains("raw 4096"),
                  "4-bit map: RLE and raw 8-bit layers disagree with MPHD 0x84 -> malformed", t.Chunks[1].Reason + " | " + t.Chunks[2].Reason);
            Check(t.Chunks[3].State == TerrainMaterialState.AlphaUnsupported, "4-bit map: nibble sums above 15 (overlay data) are not a weights check", t.Chunks[3].Reason);
            Check(t.Chunks[4].State == TerrainMaterialState.Textured && t.Count(TerrainMaterialState.Textured) == 252 && t.Raw4Layers == 3 && !t.UseHeightTextures,
                  "4-bit map: single-layer chunks have no alpha and draw textured");

            // a whole tile of 4-bit chunks: unsupported
            TerrainTileMaterial all4 = BuildTile(new Tex0Spec
            {
                Mdid = new uint[] { 1000, 1001 },
                Chunk = n => TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Uncompressed4, Constant(n % 16))),
            }, Mphd4);
            Check(all4.State == TerrainTileMaterialState.Unsupported && all4.Count(TerrainMaterialState.AlphaUnsupported) == 256 &&
                  all4.TextureIds.Length == 0 && all4.DrawableSlices == 0 && all4.Reason.Contains("alphaUnsupported 256"),
                  "tile state unsupported: no drawable chunk, no texture requested", all4.Reason);

            // MPHD 0x4 alone and 0x80 alone both announce 8-bit; only 0x80 turns the height textures on
            foreach (uint m in new uint[] { 0x24C, 0x2C8 })
            {
                TerrainTileMaterial e = BuildTile(new Tex0Spec { Chunk = n => TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1))) }, m);
                Check(e.AlphaBitDepth == 8 && e.State == TerrainTileMaterialState.Textured && e.Count(TerrainMaterialState.Textured) == 256 && e.Reason == null &&
                      e.UseHeightTextures == ((m & 0x80) != 0) && e.Chunks.All(c => c.UseHeightTextures == e.UseHeightTextures),
                      string.Format("MPHD 0x{0:X}: 8-bit; every chunk textured -> tile state textured; height textures {1}", m, (m & 0x80) != 0 ? "on" : "off"));
            }

            // MTXP entry count unlike MDID: chunks with layers malformed, chunks without layers keep noLayers
            TerrainTileMaterial bad = BuildTile(new Tex0Spec
            {
                Mdid = new uint[] { 1000, 1001, 1002, 1003 },
                Mtxp = Cat(MtxpEntry(0), MtxpEntry(0), MtxpEntry(0)),
                Chunk = n => n == 7 ? Empty() : TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1))),
            });
            Check(bad.Count(TerrainMaterialState.Malformed) == 255 && bad.Chunks[7].State == TerrainMaterialState.NoLayers &&
                  bad.Chunks[0].Reason.Contains("MTXP has 48 byte(s), not 4 entries"), "MTXP with 3 entries for 4 MDID entries -> malformed", bad.Chunks[0].Reason);
            TerrainTileMaterial odd = BuildTile(new Tex0Spec { Mdid = new uint[] { 1000, 1001 }, Mtxp = new byte[33] });
            Check(odd.Count(TerrainMaterialState.Malformed) == 256, "MTXP of 33 bytes (not whole entries) -> malformed");

            // scales 0..4 -> 8, 4, 2, 1, 0.5 repeats (one repeat per 2^s cells); the UV phase (frac(ix * r), frac(iy * r))
            // is 0 for whole repeats and (0.5 * (ix % 2), 0.5 * (iy % 2)) at 0.5; no MTXP -> 8, phase 0
            TerrainTileMaterial sc = BuildTile(new Tex0Spec
            {
                Mdid = new uint[] { 1000, 1001, 1002, 1003, 1004 },
                Mtxp = Cat(MtxpEntry(0x00), MtxpEntry(0x10), MtxpEntry(0x20), MtxpEntry(0x30), MtxpEntry(0x40)),
                Chunk = n => TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)), Layer(2, alpha: Pattern(2)), Layer(3, alpha: Pattern(3)), Layer(4, alpha: Pattern(4))),
            });
            bool phases = true;
            var badPhase = new List<string>();
            for (int n = 0; n < 256; n++)
            {
                int ix = n % 16, iy = n / 16;
                foreach (TerrainMaterialLayer l in sc.Chunks[n].Layers)
                {
                    float wu = l.ScaleExponent == 4 && ix % 2 == 1 ? 0.5f : 0f, wv = l.ScaleExponent == 4 && iy % 2 == 1 ? 0.5f : 0f;
                    if (l.PhaseU != wu || l.PhaseV != wv)
                    {
                        phases = false;
                        badPhase.Add(n + "/" + l.Layer + ": (" + l.PhaseU + ", " + l.PhaseV + ")");
                    }
                }
            }
            Check(sc.Chunks[0].Layers.Select(l => l.RepeatsPerChunk).SequenceEqual(new[] { 8f, 4f, 2f, 1f, 0.5f }) && sc.Chunks[0].Layers[3].MtxpFlags == 0x30 &&
                  sc.Chunks[0].Layers[4].MtxpFlags == 0x40 && sc.State == TerrainTileMaterialState.Textured, "MTXP scale 0..4 -> 8, 4, 2, 1, 0.5 repeats per chunk (a float)");
            Check(phases && sc.Chunks[17].Layers[4].PhaseU == 0.5f && sc.Chunks[17].Layers[4].PhaseV == 0.5f && sc.Chunks[34].Layers[4].PhaseU == 0f,
                  "UV phase on all 256 chunks: 0 for 8, 4, 2, 1 repeats; (0.5 * (ix % 2), 0.5 * (iy % 2)) for 0.5 repeats", string.Join("; ", badPhase.Take(4)));
            Check(TerrainMaterialBuilder.RepeatsForScaleExponent(4) == 0.5f && TerrainMaterialBuilder.RepeatsForScaleExponent(0) == 8f &&
                  TerrainMaterialBuilder.RepeatsForScaleExponent(5) == 0f && TerrainMaterialBuilder.RepeatsForScaleExponent(15) == 0f &&
                  TerrainMaterialBuilder.RepeatPhase(3, 0.5f) == 0.5f && TerrainMaterialBuilder.RepeatPhase(15, 0.5f) == 0.5f &&
                  TerrainMaterialBuilder.RepeatPhase(14, 0.5f) == 0f && TerrainMaterialBuilder.RepeatPhase(7, 1f) == 0f && TerrainMaterialBuilder.RepeatPhase(7, 0f) == 0f,
                  "RepeatsForScaleExponent: 8 / 2^s for s 0..4, 0 above; RepeatPhase(i, r) = frac(i * r), 0 for unresolved repeats");
            TerrainTileMaterial noMtxp = BuildTile(new Tex0Spec { Chunk = n => TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1))) });
            Check(noMtxp.Chunks.All(c => c.Layers.All(l => l.RepeatsPerChunk == 8 && l.PhaseU == 0f && l.PhaseV == 0f && !l.HasMtxp)) &&
                  noMtxp.Chunks[0].Layers.All(l => l.HeightScale == 0f && l.HeightOffset == 1f && l.MtxpWord3 == 0),
                  "no MTXP: 8 repeats, phase 0, height values (0.0, 1.0)");

            // the MDID / MHID / MTXP table rules (v3 delta, sections 2 and 5): a chunk with layers is malformed,
            // a chunk without layers stays noLayers, and nothing is requested
            uint[] four = { 1000, 1001, 1002, 1003 };
            Func<int, TexChunkSpec> withEmpty = n => n == 9 ? Empty() : TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1)));
            var tables = new[]
            {
                new { Name = "MDID twice", Spec = new Tex0Spec { Mdid = four, Chunk = withEmpty, Extra = new[] { Chunk("MDID", U32Payload(5000, 5001, 5002, 5003)) } }, Want = "MDID table is malformed" },
                new { Name = "MHID twice", Spec = new Tex0Spec { Mdid = four, Chunk = withEmpty, Extra = new[] { Chunk("MHID", U32Payload(2004, 0, 0, 0)) } }, Want = "MHID table is malformed" },
                new { Name = "MTXP twice", Spec = new Tex0Spec { Mdid = four, Chunk = withEmpty, Mtxp = Cat(MtxpEntry(0), MtxpEntry(0), MtxpEntry(0), MtxpEntry(0)),
                                                                Extra = new[] { Chunk("MTXP", Cat(MtxpEntry(0x20, 5f, 1f), MtxpEntry(0), MtxpEntry(0), MtxpEntry(0))) } }, Want = "MTXP table is malformed" },
                new { Name = "MHID of 6 bytes", Spec = new Tex0Spec { Mdid = four, Chunk = withEmpty, MhidRaw = new byte[] { 0xD4, 0x07, 0, 0, 0, 0 } }, Want = "not a whole number of u32" },
                new { Name = "MHID with 2 entries for 4 MDID entries", Spec = new Tex0Spec { Mdid = four, Chunk = withEmpty, Mhid = new uint[] { 0, 0 } }, Want = "MHID has 2 entries and MDID 4" },
                new { Name = "MHID present but empty", Spec = new Tex0Spec { Mdid = four, Chunk = withEmpty, MhidRaw = new byte[0] }, Want = "MHID has 0 entries and MDID 4" },
            };
            foreach (var x in tables)
                foreach (uint m in new[] { Mphd8, Mphd8NoHeight })
                {
                    TerrainTileMaterial tt = BuildTile(x.Spec, m);
                    Check(tt.Count(TerrainMaterialState.Malformed) == 255 && tt.Chunks[9].State == TerrainMaterialState.NoLayers && tt.Chunks[0].Reason.Contains(x.Want) &&
                          tt.TextureIds.Length == 0 && tt.HeightTextureIds.Length == 0 && tt.State == TerrainTileMaterialState.Unsupported &&
                          tt.Chunks[0].Layers.All(l => l.HeightTextureFileDataID == 0),
                          string.Format("{0} (MPHD 0x{1:X}): chunks with layers malformed, the chunk without layers noLayers, no texture requested", x.Name, m),
                          Counters(tt) + "; " + tt.Chunks[0].Reason);
                }
            TerrainTileMaterial noMhid = BuildTile(new Tex0Spec { OmitMhid = true, Chunk = n => TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1))) });
            Check(noMhid.Notes.Count == 0 && noMhid.State == TerrainTileMaterialState.Textured && noMhid.HeightTextureIds.Length == 0,
                  "no MHID chunk: no height textures (observed), no note, textured");
            TerrainTileMaterial ignored = BuildTile(new Tex0Spec { Mhid = new uint[] { 0, 2001, 0, 0 }, Chunk = n => TexChunkSpec.Of(Layer(0), Layer(1, alpha: Pattern(1))) }, Mphd8NoHeight);
            Check(ignored.State == TerrainTileMaterialState.Textured && ignored.HeightTextureIds.Length == 0 && ignored.HeightLayerReferences == 0 &&
                  ignored.Chunks[0].Layers[1].HeightTextureFileDataID == 2001 && !ignored.Chunks[0].NeedsHeightTextures &&
                  ignored.Notes.Count == 1 && ignored.Notes[0].Contains("ignored"),
                  "MHID on an MPHD 0x80-clear map: carried per layer, never requested or read, a tile note", ignored.Notes.FirstOrDefault());

            TerrainTileMaterial un = TerrainTileMaterial.Unavailable("tex0 778040: not installed locally", 0x3CA);
            Check(un.State == TerrainTileMaterialState.Unavailable && un.Chunks.Length == 0 && un.TextureIds.Length == 0 && un.HeightTextureIds.Length == 0 &&
                  TerrainTileMaterial.TileStateName(un.State) == "unavailable" && un.Reason.Contains("not installed"), "tile state unavailable (no tex0)");
            Throws<ArgumentNullException>(() => TerrainMaterialBuilder.Build(null, AdtParser.ParseTex(Tex0(null)), Mphd8), "Build needs the root");
            Throws<ArgumentNullException>(() => TerrainMaterialBuilder.Build(AdtParser.ParseRoot(Root(TX, TY), TX, TY), null, Mphd8), "Build needs the tex0");
        }

        // ================================================================================== precedence

        /// <summary>Every combination of seven conditions on one chunk: M (malformed: an MTXP height scale that is
        /// not a finite number), A (flag 0x8000 clear), L (9 layers: 8 parsed plus one appended to the parsed chunk,
        /// since AdtParser makes more than 8 MCLY entries Malformed itself), U (MCLY 0x400 on the base layer), HF (a
        /// layer whose height texture 2003 fails), S (MCLY 0x40), Z (MDID 0). (An MTXP scale nibble above the verified
        /// 0..4 is not a malformed condition: ScaleNibbleAndHeightDomainTests puts it next to every state.)</summary>
        static void PrecedenceTests()
        {
            uint[] mdid = { 1000, 1001, 0, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010 };
            uint[] mhid = new uint[mdid.Length];
            mhid[3] = 2003;
            var mtxp = new List<byte[]>();
            for (int i = 0; i < mdid.Length; i++) mtxp.Add(i == 4 ? MtxpEntry(0, float.NaN, 1f) : MtxpEntry(0));
            const int M = 1, A = 2, L = 4, U = 8, HF = 16, S = 32, Z = 64;
            Func<int, TexChunkSpec> chunk = n =>
            {
                if (n >= 128)
                    return Empty();
                var layers = new List<TexLayerSpec> { Layer(0, extraFlags: (n & U) != 0 ? 0x400u : 0u), Layer(1, alpha: Constant(20), extraFlags: (n & S) != 0 ? 0x40u : 0u) };
                if ((n & Z) != 0) layers.Add(Layer(2, alpha: Constant(20)));
                if ((n & HF) != 0) layers.Add(Layer(3, alpha: Constant(20)));
                if ((n & M) != 0) layers.Add(Layer(4, alpha: Constant(20)));
                for (uint pad = 5; (n & L) != 0 && layers.Count < 8; pad++) layers.Add(Layer(pad, alpha: Constant(20)));
                return TexChunkSpec.Of(layers.ToArray());
            };
            Action<AdtTex> ninth = tx =>
            {
                for (int n = 0; n < 128; n++)
                    if ((n & L) != 0)
                        AppendLayer(tx.Mcnk[n], 9, Constant(20));
            };
            Func<int, RootChunkSpec> root = n =>
                n < 128 ? Rs((n & A) != 0 ? 0x10000u : RootFlags) : n == 128 ? Rs(0x10000) : n == 129 ? new RootChunkSpec { Flags = RootFlags, McvtBytes = 100 } : Rs();
            var spec = new Tex0Spec { Mdid = mdid, Mhid = mhid, Mtxp = Cat(mtxp.ToArray()), Chunk = chunk };
            var order = new[] { M, A, L, U, HF, S, Z };
            var states = new[] { TerrainMaterialState.Malformed, TerrainMaterialState.AlphaUnsupported, TerrainMaterialState.LayerCountUnsupported,
                                 TerrainMaterialState.LayerEffectUnresolved, TerrainMaterialState.HeightTextureMissing, TerrainMaterialState.LayerEffectStatic,
                                 TerrainMaterialState.MissingTexture };
            foreach (uint mphd in new[] { Mphd8, Mphd8NoHeight })
            {
                TerrainTileMaterial t = BuildTile(spec, mphd, root, ninth);
                bool heights = (mphd & 0x80) != 0;
                Check(t.Chunks[L].Layers.Length == 9 && t.Chunks[L].State == TerrainMaterialState.LayerCountUnsupported &&
                      t.Chunks[L].Reason == "9 layers; at most 8 are drawn (8 is the most observed)" && t.Chunks[L].Alpha == null,
                      string.Format("precedence (MPHD 0x{0:X}): 9 layers alone -> layerCountUnsupported, named, no slice", mphd), t.Chunks[L].Reason);
                for (int phase = 0; phase < 2; phase++)
                {
                    if (phase == 1)
                        t.ApplyHeightTextureFailures(new uint[] { 2003 });
                    int ok = 0;
                    var bad = new List<string>();
                    for (int n = 0; n < 128; n++)
                    {
                        TerrainMaterialState want = TerrainMaterialState.Textured;
                        for (int i = order.Length - 1; i >= 0; i--)
                            if ((n & order[i]) != 0 && (order[i] != HF || (phase == 1 && heights)))
                                want = states[i];
                        if (t.Chunks[n].State == want) ok++;
                        else bad.Add(n + ": " + TerrainTileMaterial.StateName(t.Chunks[n].State) + " not " + TerrainTileMaterial.StateName(want));
                    }
                    Check(ok == 128, string.Format("precedence (MPHD 0x{0:X}, {1}): all 128 combinations of malformed / alphaUnsupported / layerCount / " +
                                                   "layerEffectUnresolved / heightTextureMissing / layerEffectStatic / missingTexture give the first state that applies",
                                                   mphd, phase == 0 ? "as built" : "height texture 2003 failed"),
                          ok + "/128" + (bad.Count > 0 ? "; " + string.Join("; ", bad.Take(6)) : ""));
                }
                Check(t.Chunks[128].State == TerrainMaterialState.AlphaUnsupported && t.Chunks[129].State == TerrainMaterialState.Malformed &&
                      t.Chunks[130].State == TerrainMaterialState.NoLayers, "precedence: no layers + flag 0x8000 clear -> alphaUnsupported; + a malformed root -> malformed; alone -> noLayers",
                      States(t, 128, 129, 130));
                Check(t.Chunks[127].Reason.Contains("not finite") && (!heights || t.Chunks[HF].Reason.Contains("2003")), "precedence: the reason is the winning state's",
                      t.Chunks[127].Reason + " | " + t.Chunks[HF].Reason);
                Check(t.HeightTextureIds.SequenceEqual(heights ? new uint[] { 2003 } : new uint[0]),
                      string.Format("precedence tile (MPHD 0x{0:X}): height ids {1}", mphd, heights ? "[2003]" : "none"), string.Join(" ", t.HeightTextureIds));
            }
        }

        // ================================================================================== scale nibble and height domain

        /// <summary>An MTXP texture-scale nibble above the verified 0..4 (never observed; 4 is drawn since round 3:
        /// Retail map 2923) and MTXP height values outside the height mix's domain are LayerEffectUnresolved, with
        /// the precedence around that state; the domain's boundaries; non-finite values stay Malformed; on an MPHD
        /// 0x80-clear map the height values are ignored (H = 1) and give no state, the nibble still does.</summary>
        static void ScaleNibbleAndHeightDomainTests()
        {
            // MDID entry: texture id, MHID, MTXP (flags, height scale s, height offset o)
            //   0 1000 (0, 0, 1)      1 1001 (0x40: nibble 4, drawn)    2 1002 (0x50: nibble 5)    3 1003 (0xF0: nibble 15)
            //   4 1004 (0, 0, -1)     5 1005 (0, 0, +inf)        6 1006, 2006 (0, -1, 1): s + o = 0
            //   7 1007, 2007 (0, -1.5, 1): s + o = -0.5          8 1008 (0, -5, 1): MHID 0, s unused
            //   9 1009 (0, 3e38, 3e38): MHID 0, H = o finite     10 1010 (0, 0, -0.0)       11 1011, 2011 (0, 0, 0): H = 0
            //  12 1012, 2012 (0, 1, 0): its height texture fails  13 MDID 0
            uint[] mdid = { 1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1011, 1012, 0 };
            uint[] mhid = { 0, 0, 0, 0, 0, 0, 2006, 2007, 0, 0, 0, 2011, 2012, 0 };
            byte[] mtxp = Cat(MtxpEntry(0), MtxpEntry(0x40), MtxpEntry(0x50), MtxpEntry(0xF0), MtxpEntry(0, 0f, -1f), MtxpEntry(0, 0f, float.PositiveInfinity),
                              MtxpEntry(0, -1f, 1f), MtxpEntry(0, -1.5f, 1f), MtxpEntry(0, -5f, 1f), MtxpEntry(0, 3e38f, 3e38f), MtxpEntry(0, 0f, -0f),
                              MtxpEntry(0, 0f, 0f), MtxpEntry(0, 1f, 0f), MtxpEntry(0));
            Func<uint, uint, TexLayerSpec> fx = (i, flags) => Layer(i, alpha: Constant(20), extraFlags: flags);
            Func<uint, TexLayerSpec> a = i => Layer(i, alpha: Constant(20));
            Func<int, TexChunkSpec> chunk = n =>
            {
                switch (n)
                {
                    case 0: return TexChunkSpec.Of(Layer(0), a(1));                    // nibble 4 (verified: drawn)
                    case 1: return TexChunkSpec.Of(Layer(0), a(2));                    // nibble 5
                    case 2: return TexChunkSpec.Of(Layer(3), a(0));                    // nibble 15 on the base layer
                    case 3: return TexChunkSpec.Of(Layer(0), a(2));                    // nibble 5 + flag 0x8000 clear
                    case 4: return TexChunkSpec.Of(Layer(0), a(2), a(5));              // nibble 5 + a non-finite offset
                    case 5: return TexChunkSpec.Of(Layer(0), a(2), a(0), a(0), a(0), a(0), a(0), a(0));   // nibble 5 + 9 layers (one appended below)
                    case 6: return TexChunkSpec.Of(Layer(0), fx(2, 0x40));             // nibble 5 on an MCLY 0x40 layer
                    case 7: return TexChunkSpec.Of(Layer(13), a(2));                   // nibble 5 + MDID 0
                    case 8: return TexChunkSpec.Of(Layer(0), a(2), a(12));             // nibble 5 + a failed height texture
                    case 9: return TexChunkSpec.Of(Layer(0), a(4));                    // o < 0, MHID 0
                    case 10: return TexChunkSpec.Of(Layer(0), a(4));                   // o < 0 + flag 0x8000 clear
                    case 11: return TexChunkSpec.Of(Layer(0), a(4), a(5));             // o < 0 + a non-finite offset
                    case 12: return TexChunkSpec.Of(Layer(0), fx(4, 0x40));            // o < 0 on an MCLY 0x40 layer
                    case 13: return TexChunkSpec.Of(Layer(13), a(4));                  // o < 0 + MDID 0
                    case 14: return TexChunkSpec.Of(Layer(0), a(4), a(12));            // o < 0 + a failed height texture
                    case 15: return TexChunkSpec.Of(Layer(0), a(6));                   // s + o = 0 with a height texture
                    case 16: return TexChunkSpec.Of(Layer(0), a(7));                   // s + o = -0.5 with a height texture
                    case 17: return TexChunkSpec.Of(Layer(0), a(8));                   // s = -5, MHID 0
                    case 18: return TexChunkSpec.Of(Layer(0), a(9));                   // (3e38, 3e38), MHID 0
                    case 19: return TexChunkSpec.Of(Layer(0), a(10));                  // o = -0.0
                    case 20: return TexChunkSpec.Of(Layer(11), a(11));                 // H = 0 everywhere (S == 0)
                    case 21: return TexChunkSpec.Of(Layer(5));                         // a non-finite offset alone
                    default: return TexChunkSpec.Of(Layer(0), a(12));
                }
            };
            var spec = new Tex0Spec { Mdid = mdid, Mhid = mhid, Mtxp = mtxp, Chunk = chunk };
            Func<int, RootChunkSpec> root = n => n == 3 || n == 10 ? Rs(0x10000) : Rs();
            Action<AdtTex> ninth = tx => AppendLayer(tx.Mcnk[5], 0, Constant(20));
            TerrainMaterialState U = TerrainMaterialState.LayerEffectUnresolved, T = TerrainMaterialState.Textured, M = TerrainMaterialState.Malformed,
                                 A = TerrainMaterialState.AlphaUnsupported, L = TerrainMaterialState.LayerCountUnsupported;
            var onWant = new[] { T, U, U, A, M, L, U, U, U, U, A, M, U, U, U, T, U, T, T, T, T, M };
            var offWant = new[] { T, U, U, A, M, L, U, U, U, T, A, M, TerrainMaterialState.LayerEffectStatic, TerrainMaterialState.MissingTexture, T, T, T, T, T, T, T, M };
            foreach (uint mphd in new[] { Mphd8, Mphd8NoHeight })
            {
                TerrainTileMaterial t = BuildTile(spec, mphd, root, ninth);
                bool heights = (mphd & 0x80) != 0;
                TerrainMaterialState[] want = heights ? onWant : offWant;
                for (int phase = 0; phase < 2; phase++)
                {
                    if (phase == 1)
                        t.ApplyHeightTextureFailures(new uint[] { 2012 });
                    var bad = new List<string>();
                    for (int n = 0; n < 256; n++)
                    {
                        TerrainMaterialState w = n < want.Length ? want[n] : phase == 1 && heights ? TerrainMaterialState.HeightTextureMissing : T;
                        if (t.Chunks[n].State != w)
                            bad.Add(n + ": " + TerrainTileMaterial.StateName(t.Chunks[n].State) + " not " + TerrainTileMaterial.StateName(w) + " (" + t.Chunks[n].Reason + ")");
                    }
                    Check(bad.Count == 0, string.Format("nibble above 4 and height domain (MPHD 0x{0:X}, {1}): every chunk's state, layerEffectUnresolved below " +
                                                        "malformed / alphaUnsupported / layerCount and above heightTextureMissing / layerEffectStatic / missingTexture{2}",
                                                        mphd, phase == 0 ? "as built" : "height texture 2012 failed",
                                                        heights ? "" : "; the height values ignored (H = 1), the nibble still unresolved"),
                          bad.Count + " wrong" + (bad.Count > 0 ? ": " + string.Join("; ", bad.Take(4)) : ""));
                }
                Check(t.Chunks[1].Reason == "layer 1 (texture 1002): MTXP entry 2 has flags 0x50: MTXP texture scale nibble 5: never observed (0..4 verified)" &&
                      t.Chunks[2].Reason.StartsWith("layer 0 (texture 1003)", StringComparison.Ordinal) &&
                      t.Chunks[2].Reason.EndsWith("MTXP texture scale nibble 15: never observed (0..4 verified)", StringComparison.Ordinal) &&
                      t.Chunks[1].Layers[1].RepeatsPerChunk == 0f && t.Chunks[1].PlainDiagnostic && t.Chunks[5].Layers.Length == 9 &&
                      t.Chunks[0].Reason == null && t.Chunks[0].Layers[1].RepeatsPerChunk == 0.5f && t.Chunks[0].Drawable,
                      string.Format("MPHD 0x{0:X}: the nibble's reason names the layer, texture, MTXP entry, flags and nibble; nibble 4 is drawn (0.5 repeats)", mphd),
                      t.Chunks[1].Reason);
                Check(t.Chunks[4].Reason.Contains("not finite") && t.Chunks[11].Reason.Contains("not finite") && t.Chunks[21].Reason.Contains("not finite"),
                      string.Format("MPHD 0x{0:X}: a non-finite height value stays malformed (above the unresolved nibble and offset)", mphd), t.Chunks[11].Reason);
                if (heights)
                {
                    Check(t.Chunks[9].Reason == "layer 1 (texture 1004): MTXP entry 4 has height scale s = 0 and offset o = -1 (MHID entry 0: H = o): " +
                                                 "MTXP height values let H = h*s + o fall below 0 or overflow float32; the height mix is not defined there" &&
                          t.Chunks[16].Reason.Contains("s = -1.5 and offset o = 1 (height texture 2007)"),
                          "height domain reason: layer, texture, MTXP entry, s, o and the height texture (or H = o)", t.Chunks[9].Reason + " | " + t.Chunks[16].Reason);
                    // plain-only: the unresolved chunks' ids, without those of layers of unresolved repeats (1002 nibble 5, 1003
                    // nibble 15: drawn as the checker, never bound) and without the drawable chunks' (1000, 1001, 1012, ...)
                    Check(t.HeightTextureIds.SequenceEqual(new uint[] { 2006, 2011, 2012 }) && t.PlainOnlyTextureIds.SequenceEqual(new uint[] { 1004, 1007 }),
                          "height domain: unresolved chunks request no height texture; their diffuse ids are plain-only (boundary chunks 15 and 20 are drawable), " +
                          "except the textures of layers of unresolved repeats", string.Join(" ", t.HeightTextureIds) + " | plain " + string.Join(" ", t.PlainOnlyTextureIds));
                }
            }
        }

        // ================================================================================== 6..8 layers, nibble 4 phase, nibble 5

        /// <summary>
        /// Round 3: chunks of 6, 7 and 8 layers drawn with the height mix (alpha layers 5..7 from the chunk's second
        /// slice, a height texture on a second-slice layer) against exact hand values; the slice layout and
        /// numbering (second slice right after the first, drawable then plain-diagnostic); MTXP scale nibble 4 at
        /// 0.5 repeats with the chunk's UV phase (0.5 * (ix % 2), 0.5 * (iy % 2)) on the diffuse and the height
        /// texture, at even and odd ix and iy, continuous across the chunk edge; nibble 5 (never observed): the
        /// checker in its layer's place, its texture never listed or read, its height texture refused.
        /// </summary>
        static void ManyLayersAndNibble4Tests()
        {
            // MDID entry: texture, MHID, MTXP (flags, height scale, height offset); on MPHD 0x80 with MHID 0, H = offset
            //   0..7  3000..3007 (constant colours), heights 1, 2, 0.5, 1.5, 3, 2.5, 4006 (alpha 0.4) x 5 + 1 = 3, 4
            //   8     3100: 4 x 4, R = x / 4, G = y / 4, nibble 4 (0x40), H = 1
            //   9     3102: blue, nibble 4, height texture 3101 (2 x 2 alpha 0, 0.5 / 1, 0.25) x 4 + 0.5
            //   10    3200: nibble 5 (0x50), height texture 3201
            uint[] mdid = { 3000, 3001, 3002, 3003, 3004, 3005, 3006, 3007, 3100, 3102, 3200 };
            uint[] mhid = { 0, 0, 0, 0, 0, 0, 4006, 0, 0, 3101, 3201 };
            byte[] mtxp = Cat(MtxpEntry(0, 0f, 1f), MtxpEntry(0, 0f, 2f), MtxpEntry(0, 0f, 0.5f), MtxpEntry(0, 0f, 1.5f), MtxpEntry(0, 0f, 3f),
                              MtxpEntry(0, 0f, 2.5f), MtxpEntry(0, 5f, 1f), MtxpEntry(0, 0f, 4f), MtxpEntry(0x40), MtxpEntry(0x40, 4f, 0.5f), MtxpEntry(0x50));
            int[] alphas = { 10, 20, 30, 40, 50, 60, 35 };
            Func<int, TexLayerSpec[]> stack = count => Enumerable.Range(0, count).Select(k => k == 0 ? Layer(0) : Layer((uint)k, alpha: Constant(alphas[k - 1]))).ToArray();
            int[] n4Single = { 0, 1, 2, 16, 17, 255 };
            int[] n4Height = { 32, 33, 48, 49 };
            Func<int, TexChunkSpec> chunk = n =>
            {
                if (n4Single.Contains(n)) return TexChunkSpec.Of(Layer(8));
                if (n4Height.Contains(n)) return TexChunkSpec.Of(Layer(0), Layer(9, alpha: Constant(51)));
                switch (n)
                {
                    case 64: return TexChunkSpec.Of(stack(6));
                    case 65: return TexChunkSpec.Of(stack(7));
                    case 66: return TexChunkSpec.Of(stack(8));
                    case 67: return TexChunkSpec.Of(stack(6).Concat(new[] { Layer(10, alpha: Constant(60)) }).ToArray());
                    case 68: return TexChunkSpec.Of(Layer(0), Layer(10, alpha: Constant(255)));
                    default: return TexChunkSpec.Of(Layer(0));
                }
            };
            var spec = new Tex0Spec { Mdid = mdid, Mhid = mhid, Mtxp = mtxp, Chunk = chunk };
            TerrainTileMaterial on = BuildTile(spec, Mphd8), off = BuildTile(spec, Mphd8NoHeight);
            var ramp = new float[48];
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) { ramp[3 * (y * 4 + x)] = x / 4f; ramp[3 * (y * 4 + x) + 1] = y / 4f; }
            var tex = new Dictionary<uint, TerrainTexels>
            {
                { 3000, TerrainTexels.Constant(1f, 0f, 0f) }, { 3001, TerrainTexels.Constant(0f, 1f, 0f) }, { 3002, TerrainTexels.Constant(0f, 0f, 1f) },
                { 3003, TerrainTexels.Constant(1f, 1f, 0f) }, { 3004, TerrainTexels.Constant(0f, 1f, 1f) }, { 3005, TerrainTexels.Constant(1f, 0f, 1f) },
                { 3006, TerrainTexels.Constant(0.5f, 0.25f, 0.125f) }, { 3007, TerrainTexels.Constant(0.2f, 0.4f, 0.6f) },
                { 4006, TerrainTexels.Constant(1f, 1f, 1f, 0.4f) }, { 3100, TerrainTexels.FromRgb(4, 4, ramp) }, { 3102, TerrainTexels.Constant(0f, 0f, 1f) },
                { 3101, TerrainTexels.FromRgba(2, 2, new[] { 1f, 1f, 1f, 0f, 1f, 1f, 1f, 0.5f, 1f, 1f, 1f, 1f, 1f, 1f, 1f, 0.25f }) },
            };
            Func<uint, TerrainTexels> lookup = id => tex.TryGetValue(id, out TerrainTexels x) ? x : null;   // 3200 and 3201 are never read

            // states, lists, slices
            Check(States(on, 0, 1, 32, 33, 64, 65, 66, 67, 68) ==
                  "0:textured, 1:textured, 32:textured, 33:textured, 64:textured, 65:textured, 66:textured, 67:layerEffectUnresolved, 68:layerEffectUnresolved" &&
                  on.Chunks[67].Reason.Contains("nibble 5: never observed") && on.Count(TerrainMaterialState.Textured) == 254,
                  "6, 7 and 8 layers and nibble 4 are textured; a 7-layer chunk with a nibble-5 layer is layerEffectUnresolved", States(on, 64, 65, 66, 67, 68));
            Check(on.TextureIds.SequenceEqual(new uint[] { 3100, 3000, 3102, 3001, 3002, 3003, 3004, 3005, 3006, 3007 }) && on.PlainOnlyTextureIds.Length == 0 &&
                  on.HeightTextureIds.SequenceEqual(new uint[] { 3101, 4006 }),
                  "lists: the second-slice layers' textures and height texture are listed; nibble 5's texture 3200 is never listed (not even plain-only: drawn as " +
                  "the checker) nor its height texture 3201", string.Join(" ", on.TextureIds) + " | plain " + string.Join(" ", on.PlainOnlyTextureIds) +
                  " | heights " + string.Join(" ", on.HeightTextureIds));
            Check(on.Chunks[64].AlphaSlice == 64 && on.Chunks[64].AlphaSlice2 == 65 && on.Chunks[65].AlphaSlice == 66 && on.Chunks[65].AlphaSlice2 == 67 &&
                  on.Chunks[66].AlphaSlice == 68 && on.Chunks[66].AlphaSlice2 == 69 && on.Chunks[69].AlphaSlice == 70 && on.Chunks[69].AlphaSlice2 == -1 &&
                  on.DrawableSlices == 254 + 3 && on.Chunks[67].AlphaSlice == 257 && on.Chunks[67].AlphaSlice2 == 258 && on.Chunks[68].AlphaSlice == 259 &&
                  on.Chunks[68].AlphaSlice2 == -1 && on.PlainSlices == 3,
                  "slices: a chunk of 6..8 layers takes two (its second right after its first), drawable chunks first, then the plain-diagnostic ones (two for chunk 67)",
                  on.DrawableSlices + " + " + on.PlainSlices);
            byte[] all = on.BuildAlphaArray(true), drawn = on.BuildAlphaArray(false);
            TerrainChunkMaterial c66 = on.Chunks[66], c67 = on.Chunks[67];
            bool layout = all.Length == 260 * 16384 && drawn.Length == 257 * 16384 && drawn.SequenceEqual(all.Take(257 * 16384)) &&
                          all.Skip(68 * 16384).Take(2 * 16384).SequenceEqual(c66.Alpha) && all.Skip(257 * 16384).Take(2 * 16384).SequenceEqual(c67.Alpha);
            for (int i = 0; i < 4096; i++)
                for (int k = 1; k <= 8; k++)
                {
                    byte want = k <= 7 ? (byte)alphas[k - 1] : (byte)0;
                    layout &= c66.Alpha[16384 * ((k - 1) / 4) + 4 * i + (k - 1) % 4] == want;
                }
            Check(layout, "alpha array: layers 1..4 in the first slice's R, G, B, A, layers 5..7 in the second slice's R, G, B (A = 0), placed at AlphaSlice and AlphaSlice2");

            // 6, 7 and 8 layers: the height mix (hand values, exact fractions) with the heights on, and H = 1 on the 0x80-clear map
            var want8 = new Dictionary<string, double[]>
            {
                { "64 on", new[] { 0.651909942, 0.413609917, 0.640273210 } }, { "64 off", new[] { 0.769968051, 0.266544957, 0.383386581 } },
                { "65 on", new[] { 0.544738062, 0.391052388, 0.509851646 } }, { "65 off", new[] { 0.615447823, 0.361955629, 0.457888250 } },
                { "66 on", new[] { 0.451424288, 0.406409295, 0.547582459 } }, { "66 off", new[] { 0.503906899, 0.419700748, 0.543516209 } },
            };
            foreach (var e in want8)
            {
                int n = int.Parse(e.Key.Split(' ')[0]);
                TerrainTileMaterial m = e.Key.EndsWith("on", StringComparison.Ordinal) ? on : off;
                float r, g, b;
                TerrainCompositor.Compose(m.Chunks[n], lookup, 3.3f, 5.6f, false, out r, out g, out b);
                Check(Near(r, e.Value[0]) && Near(g, e.Value[1]) && Near(b, e.Value[2]),
                      string.Format("height mix, {0} layers: alpha 10, 20, 30, 40, 50, 60, 35 (layers 5..7 from the second slice), {1} -> hand values",
                                    m.Chunks[n].Layers.Length, e.Key.EndsWith("on", StringComparison.Ordinal)
                                        ? "MPHD 0x80: H 1, 2, 0.5, 1.5, 3, 2.5, 3 (height texture 4006 on layer 6), 4" : "MPHD 0x80 clear: H = 1"),
                      string.Format(System.Globalization.CultureInfo.InvariantCulture, "got ({0:R}, {1:R}, {2:R}), want ({3}, {4}, {5})", r, g, b, e.Value[0], e.Value[1], e.Value[2]));
            }

            // nibble 4: s = (u, v) * 0.5 / 8 + (0.5 * (ix % 2), 0.5 * (iy % 2)); texel floor(s * 4) mod 4 of the 4 x 4 ramp
            var probes = new[]
            {
                // chunk, u, v, texel x, texel y
                new[] { 0, 0.5f, 0.5f, 0, 0 }, new[] { 0, 7.9f, 7.9f, 1, 1 }, new[] { 1, 0.5f, 0.5f, 2, 0 }, new[] { 1, 7.9f, 7.9f, 3, 1 },
                new[] { 16, 0.5f, 0.5f, 0, 2 }, new[] { 16, 7.9f, 7.9f, 1, 3 }, new[] { 17, 0.5f, 0.5f, 2, 2 }, new[] { 17, 7.9f, 7.9f, 3, 3 },
                new[] { 2, 0.5f, 0.5f, 0, 0 }, new[] { 255, 7.9f, 7.9f, 3, 3 },
            };
            bool ph = true;
            var got = new List<string>();
            foreach (float[] p in probes)
            {
                float r, g, b;
                TerrainCompositor.Compose(on.Chunks[(int)p[0]], lookup, p[1], p[2], false, out r, out g, out b);
                ph &= r == p[3] / 4f && g == p[4] / 4f;
                got.Add((int)p[0] + "@" + p[1] + ": " + r + "," + g);
            }
            Check(ph, "nibble 4: 0.5 repeats plus the chunk's phase -- (0, 0) at even ix/iy (chunks 0, 2), 0.5 at odd ix (1, 17, 255) and odd iy (16, 17, 255)",
                  string.Join("; ", got));
            float s0u, s0v, s1u, s1v, s16u, s16v;
            TerrainCompositor.LayerCoordinate(on.Chunks[0].Layers[0], 8f, 8f, out s0u, out s0v);
            TerrainCompositor.LayerCoordinate(on.Chunks[1].Layers[0], 0f, 8f, out s1u, out s1v);
            TerrainCompositor.LayerCoordinate(on.Chunks[16].Layers[0], 8f, 0f, out s16u, out s16v);
            Check(s0u == 0.5f && s1u == 0.5f && s0v == 0.5f && s16v == 0.5f && s1v == 0.5f && s16u == 0.5f,
                  "nibble 4: the texture coordinate is continuous across chunk edges (chunk 0's east edge = chunk 1's west edge = 0.5; south edge = chunk 16's north edge)",
                  s0u + " " + s1u + " / " + s0v + " " + s16v);
            var hw = new[] { new[] { 32, 0.963855422, 0.036144578 }, new[] { 33, 0.695652174, 0.304347826 }, new[] { 48, 0.444444444, 0.555555556 },
                             new[] { 49, 0.842105263, 0.157894737 } };
            foreach (double[] e in hw)
            {
                float r, g, b;
                TerrainCompositor.Compose(on.Chunks[(int)e[0]], lookup, 0.5f, 0.5f, false, out r, out g, out b);
                Check(Near(r, e[1]) && g == 0f && Near(b, e[2]),
                      string.Format("nibble 4 height texture at the layer's coordinate, chunk {0} (ix {1}, iy {2}): height texel ({3}, {4}) -> weights ({5}, {6})",
                                    (int)e[0], (int)e[0] % 16, (int)e[0] / 16, (int)e[0] % 2, ((int)e[0] / 16) % 2, e[1], e[2]),
                      r + " " + g + " " + b);
            }

            // nibble 5: never observed -> repeats 0: the checker in the layer's place (the texture is never read), its
            // height texture refused, in the plain-weights diagnostic and at LayerTexel
            TerrainChunkMaterial c68 = on.Chunks[68];
            float[] grid = TerrainCompositor.ComposeGrid(c68, lookup, 128, false, TerrainBlendRule.PlainWeights);
            bool checker = true;
            for (int y = 0; y < 128; y++)
                for (int x = 0; x < 128; x++)
                {
                    float m = (x / 2 + y / 2) % 2 == 0 ? 1f : 0f;
                    int o = 4 * (y * 128 + x);
                    checker &= grid[o] == m && grid[o + 1] == 0f && grid[o + 2] == m && grid[o + 3] == 1f;
                }
            Check(checker && c68.Layers[1].RepeatsPerChunk == 0f, "nibble 5 (repeats 0), alpha 255, plain weights: the whole 128-px grid is the magenta/black " +
                  "checker of 1/8-cell squares (as the player draws it), texture 3200 never read");
            float lr, lg, lb, kr, kg, kb;
            TerrainCompositor.LayerTexel(c68.Layers[1], lookup, 0.0625f, 0.0625f, out lr, out lg, out lb);
            TerrainCompositor.LayerTexel(c68.Layers[1], lookup, 0.1875f, 0.0625f, out kr, out kg, out kb);
            Check(lr == 1f && lg == 0f && lb == 1f && kr == 0f && kg == 0f && kb == 0f, "LayerTexel: a layer of unresolved repeats is the checker, not a stretched texel");
            float cr, cg, cb;
            Throws<ArgumentException>(() => TerrainCompositor.Compose(c68, lookup, 1f, 1f, false, out cr, out cg, out cb),
                                      "height mix: the height texture of a layer of unresolved repeats has no coordinate (refused)", "unresolved repeats");
            float[] g67 = TerrainCompositor.ComposeGrid(c67, lookup, 16, false, TerrainBlendRule.PlainWeights);
            Check(g67.Length == 16 * 16 * 4 && c67.AlphaSliceCount == 2, "plain weights: a 7-layer plain-diagnostic chunk (nibble-5 layer from its second slice) composes");
        }

        // ================================================================================== textures

        static Tex0Spec ListSpec()
        {
            return new Tex0Spec
            {
                Mdid = new uint[] { 1000, 1001, 1002, 0, 1004, 1005 },
                Mhid = new uint[] { 0, 0, 2002, 0, 2004, 0 },
                Chunk = n => n == 0 ? TexChunkSpec.Of(Layer(1), Layer(0, alpha: Pattern(1)))
                           : n == 1 ? TexChunkSpec.Of(Layer(0), Layer(2, alpha: Pattern(2)))
                           : n == 2 ? TexChunkSpec.Of(Layer(5))
                           : n == 3 ? TexChunkSpec.Of(Layer(0), Layer(4, alpha: Pattern(3)))
                           : n == 4 ? TexChunkSpec.Of(Layer(3), Layer(1, alpha: Pattern(4)))
                           : n == 5 ? TexChunkSpec.Of(Layer(0, extraFlags: 0x400), Layer(5, alpha: Pattern(1)))
                           : n == 6 ? TexChunkSpec.Of(Layer(4), Layer(1, alpha: Pattern(2), extraFlags: 0x40))
                           : Empty(),
            };
        }

        static void TextureListTests()
        {
            TerrainTileMaterial t = BuildTile(ListSpec(), Mphd8, n => n == 2 ? Rs(0x10000) : Rs());
            Check(States(t, 0, 1, 2, 3, 4, 5, 6) == "0:textured, 1:textured, 2:alphaUnsupported, 3:textured, 4:missingTexture, 5:layerEffectUnresolved, 6:layerEffectStatic",
                  "list tile: the states as built", States(t, 0, 1, 2, 3, 4, 5, 6));
            Check(t.TextureIds.SequenceEqual(new uint[] { 1001, 1000, 1002, 1004 }) && t.PlainOnlyTextureIds.SequenceEqual(new uint[] { 1005 }),
                  "diffuse ids: drawable chunks only (layerEffectStatic included), first use (chunk order, then layer order), no 0; plain-only ids apart",
                  string.Join(" ", t.TextureIds) + " | plain " + string.Join(" ", t.PlainOnlyTextureIds));
            Check(t.LayerReferences == 10 && t.ZeroIdReferences == 1 && t.DuplicateReferences == 5 &&
                  t.LayerReferences == t.TextureIds.Length + t.ZeroIdReferences + t.DuplicateReferences,
                  "references: 10 layers, 1 naming id 0, 5 naming an id already listed");
            Check(t.HeightTextureIds.SequenceEqual(new uint[] { 2002, 2004 }) && t.HeightLayerReferences == 3 && t.HeightDuplicateReferences == 1 &&
                  t.HeightLayerReferences == t.HeightTextureIds.Length + t.HeightDuplicateReferences && !t.TextureIds.Intersect(t.HeightTextureIds).Any(),
                  "height ids (MPHD 0x80): drawable chunks only, first use, apart from the diffuse ids; 3 references, 1 repeat",
                  string.Join(" ", t.HeightTextureIds));
            Check(t.Chunks[0].AlphaSlice == 0 && t.Chunks[1].AlphaSlice == 1 && t.Chunks[3].AlphaSlice == 2 && t.Chunks[4].AlphaSlice == 3 &&
                  t.Chunks[6].AlphaSlice == 4 && t.Chunks[5].AlphaSlice == 5 && t.Chunks[2].AlphaSlice == -1 && t.DrawableSlices == 5 && t.PlainSlices == 1,
                  "slices: drawable chunks 0, 1, 3, 4, 6 -> 0..4; the layerEffectUnresolved chunk 5 -> 5; the neutral chunk none");
            byte[] arr = t.BuildAlphaArray(false), all = t.BuildAlphaArray(true);
            Check(arr.Length == 5 * 16384 && all.Length == 6 * 16384 && arr.Skip(4 * 16384).Take(16384).SequenceEqual(t.Chunks[6].Alpha) &&
                  all.Skip(5 * 16384).SequenceEqual(t.Chunks[5].Alpha), "alpha array: slices in slice order (plain-diagnostic ones only when asked)");
            Check(t.Chunks[4].Layers[0].Missing && !t.Chunks[4].Layers[1].Missing && t.Chunks[4].Reason.Contains("texture id 0"),
                  "missing texture: MDID 0 marks the layer and the chunk", t.Chunks[4].Reason);

            t.ApplyTextureFailures(new uint[] { 1002, 0, 1002, 1005, 7777 });
            Check(t.FailedTextureIds.SequenceEqual(new uint[] { 1002, 1005, 7777 }) && t.Chunks[1].State == TerrainMaterialState.MissingTexture &&
                  t.Chunks[1].Layers[1].Missing && !t.Chunks[1].Layers[0].Missing && t.Chunks[1].Reason.Contains("1002") &&
                  t.Chunks[5].State == TerrainMaterialState.LayerEffectUnresolved && t.Chunks[5].Layers[1].Missing &&
                  t.Count(TerrainMaterialState.Textured) == 2 && t.Count(TerrainMaterialState.MissingTexture) == 2 && t.TextureIds.Length == 4 && t.Chunks[1].AlphaSlice == 1,
                  "failed diffuse texture: its drawable chunks become missingTexture (the plain-diagnostic layer is marked too); lists and slices unchanged",
                  t.Chunks[1].Reason);
            t.ApplyHeightTextureFailures(new uint[] { 2004, 0, 2004 });
            Check(t.FailedHeightTextureIds.SequenceEqual(new uint[] { 2004 }) && t.Chunks[3].State == TerrainMaterialState.HeightTextureMissing &&
                  t.Chunks[6].State == TerrainMaterialState.HeightTextureMissing && t.Chunks[3].Layers[1].HeightMissing && !t.Chunks[3].Layers[0].HeightMissing &&
                  t.Chunks[3].Reason.Contains("2004") && t.Chunks[1].State == TerrainMaterialState.MissingTexture && !t.Chunks[3].Drawable && t.Chunks[3].PlainDiagnostic &&
                  t.Chunks[3].AlphaSlice == 2 && t.Count(TerrainMaterialState.HeightTextureMissing) == 2 && t.DrawableChunks == 3 && t.DrawableSlices == 5,
                  "failed height texture: its drawable chunks (layerEffectStatic too) become heightTextureMissing; diffuse failures kept; slices unchanged",
                  States(t, 0, 1, 3, 4, 6) + "; " + t.Chunks[3].Reason);
            t.ApplyTextureFailures(new uint[] { 1001 });
            Check(t.Chunks[1].State == TerrainMaterialState.Textured && !t.Chunks[1].Layers[1].Missing && t.Chunks[0].State == TerrainMaterialState.MissingTexture &&
                  t.Chunks[4].State == TerrainMaterialState.MissingTexture && t.Chunks[4].Reason.Contains("texture id 0") && t.Chunks[4].Reason.Contains("1001 failed") &&
                  t.Chunks[6].State == TerrainMaterialState.HeightTextureMissing && t.Chunks[6].Reason.Contains("1001 failed") && t.Chunks[6].Layers[1].Missing,
                  "diffuse failures re-applied from the built states (a new list replaces the old one); the height failure stays", t.Chunks[6].Reason);
            t.ApplyHeightTextureFailures(null);
            Check(t.Chunks[3].State == TerrainMaterialState.Textured && t.Chunks[6].State == TerrainMaterialState.LayerEffectStatic &&
                  t.Chunks[6].Reason.Contains("static composite") && t.Chunks[6].Reason.Contains("1001 failed") && t.FailedHeightTextureIds.Length == 0,
                  "no height failures: layerEffectStatic keeps precedence over a failed diffuse texture", t.Chunks[6].Reason);
            t.ApplyTextureFailures(null);
            Check(t.Count(TerrainMaterialState.Textured) == 3 && t.Count(TerrainMaterialState.MissingTexture) == 1 && t.FailedTextureIds.Length == 0 &&
                  t.Chunks[4].Layers[0].Missing && !t.Chunks[0].Layers[0].Missing && !t.Chunks[5].Layers[1].Missing && t.Chunks[6].Reason.IndexOf("failed", StringComparison.Ordinal) < 0,
                  "no failures: the built states return");

            // the same tex0 on an MPHD 0x80-clear map: no height ids, height failures change nothing
            TerrainTileMaterial off = BuildTile(ListSpec(), Mphd8NoHeight, n => n == 2 ? Rs(0x10000) : Rs());
            off.ApplyHeightTextureFailures(new uint[] { 2002, 2004 });
            Check(off.HeightTextureIds.Length == 0 && off.HeightLayerReferences == 0 && off.Count(TerrainMaterialState.HeightTextureMissing) == 0 &&
                  off.TextureIds.SequenceEqual(t.TextureIds) && off.Chunks.All(c => c.Layers.All(l => !l.HeightMissing)) && off.Notes.Count == 1,
                  "MPHD 0x80 clear: no height ids, a height failure changes no state", Counters(off));
        }

        // ================================================================================== compositor

        static void CompositorTests()
        {
            // texel index
            Check(TerrainCompositor.TexelIndex(0f, 4) == 0 && TerrainCompositor.TexelIndex(0.24f, 4) == 0 && TerrainCompositor.TexelIndex(0.25f, 4) == 1 &&
                  TerrainCompositor.TexelIndex(0.99f, 4) == 3 && TerrainCompositor.TexelIndex(1f, 4) == 0 && TerrainCompositor.TexelIndex(2.6f, 4) == 2 &&
                  TerrainCompositor.TexelIndex(-0.1f, 4) == 3, "texel index: floor(s * size) mod size");

            // alpha bilinear: a linear ramp value(row, col) = row + 2 col is reproduced exactly inside and clamped at the edges
            var slice = new byte[16384];
            for (int r = 0; r < 64; r++)
                for (int c = 0; c < 64; c++)
                    slice[4 * (r * 64 + c) + 1] = (byte)(r + 2 * c);
            double worst = 0;
            int points = 0;
            foreach (float u in new[] { 0f, 0.01f, 0.0625f, 0.07f, 1.3f, 3.99f, 4f, 6.123f, 7.93f, 7.99f, 8f })
                foreach (float v in new[] { 0f, 0.05f, 0.5f, 2.71f, 7.9375f, 7.97f, 8f })
                {
                    double x = Math.Min(63, Math.Max(0, u * 8.0 - 0.5)), y = Math.Min(63, Math.Max(0, v * 8.0 - 0.5));
                    worst = Math.Max(worst, Math.Abs(TerrainCompositor.AlphaAt(slice, 2, u, v) - (y + 2 * x) / 255.0));
                    points++;
                }
            Check(worst <= 2e-6, "alpha: manual bilinear at x = 8u - 0.5 (texel centres), clamped to texels 0..63, of a linear ramp", points + " points, max error " + worst);
            Check(TerrainCompositor.AlphaAt(slice, 2, (10 + 0.5f) / 8f, (20 + 0.5f) / 8f) == (20 + 20) / 255f && TerrainCompositor.AlphaAt(slice, 1, 3f, 3f) == 0f,
                  "alpha: at a texel centre the stored value exactly; channel = layer - 1");
            Throws<ArgumentOutOfRangeException>(() => TerrainCompositor.AlphaAt(slice, 8, 1, 1), "alpha: layer 8 is in no slice (alpha layers are 1..7)", "1..7");
            Throws<ArgumentOutOfRangeException>(() => TerrainCompositor.AlphaAt(slice, 0, 1, 1), "alpha: layer 0 (the base layer) has no alpha", "1..7");
            Throws<ArgumentException>(() => TerrainCompositor.AlphaAt(slice, 5, 1, 1), "alpha: layer 5 needs the chunk's second slice", "second slice");
            var two = new byte[2 * 16384];
            Array.Copy(slice, 0, two, 16384, 16384);
            Check(TerrainCompositor.AlphaAt(two, 6, (10 + 0.5f) / 8f, (20 + 0.5f) / 8f) == (20 + 20) / 255f && TerrainCompositor.AlphaAt(two, 2, 3f, 3f) == 0f &&
                  TerrainCompositor.AlphaAt(two, 5, 3f, 3f) == 0f,
                  "alpha: layers 5..7 from the second slice, channel (k - 1) % 4 (the ramp copied there is read as layer 6, not layer 2)");

            // MCCV fan: hand values in cell (row 2, col 3), colours = vertex index
            var idx = new float[435];
            for (int k = 0; k < 145; k++) { idx[3 * k] = k; idx[3 * k + 1] = 2 * k; idx[3 * k + 2] = 0; }
            var cases = new[]
            {
                new[] { 3.5f, 2.5f, 46f }, new[] { 3f, 2f, 37f }, new[] { 3.5f, 2f, 37.5f }, new[] { 3.25f, 2.1f, 38.95f },
                new[] { 3.9f, 2.5f, 46.4f }, new[] { 3.5f, 2.95f, 53.65f }, new[] { 3.05f, 2.5f, 45.55f }, new[] { 8f, 8f, 144f }, new[] { 0f, 0f, 0f },
            };
            bool fan = true;
            var got = new List<string>();
            foreach (float[] cs in cases)
            {
                float r, g, b;
                TerrainCompositor.VertexColorAt(idx, cs[0], cs[1], out r, out g, out b);
                fan &= Near(r, cs[2], 2e-5) && Near(g, 2 * cs[2], 4e-5) && b == 0;
                got.Add(r.ToString("0.#####", System.Globalization.CultureInfo.InvariantCulture));
            }
            Check(fan, "MCCV: barycentric over the four-triangle fan (centre, corner, edge midpoint, north/east/south/west triangles) = hand values", string.Join(" ", got));
            // an affine colour field is reproduced exactly by any triangle interpolation
            var aff = new float[435];
            for (int k = 0; k < 145; k++)
            {
                float u, v;
                TerrainMaterialBuilder.VertexUv(k, out u, out v);
                aff[3 * k] = 0.3f + 0.05f * u + 0.02f * v; aff[3 * k + 1] = 1.5f - 0.1f * v; aff[3 * k + 2] = 0.7f;
            }
            double affWorst = 0;
            var rng = new Random(9);
            for (int i = 0; i < 2000; i++)
            {
                float u = (float)(rng.NextDouble() * 8), v = (float)(rng.NextDouble() * 8), r, g, b;
                TerrainCompositor.VertexColorAt(aff, u, v, out r, out g, out b);
                affWorst = Math.Max(affWorst, Math.Max(Math.Abs(r - (0.3 + 0.05 * u + 0.02 * v)), Math.Max(Math.Abs(g - (1.5 - 0.1 * v)), Math.Abs(b - 0.7))));
            }
            Check(affWorst <= 2e-6, "MCCV: an affine colour field is reproduced at 2,000 random points", "max error " + affWorst);
            float n1, n2, n3;
            TerrainCompositor.VertexColorAt(null, 2, 2, out n1, out n2, out n3);
            Check(n1 == 1f && n2 == 1f && n3 == 1f, "MCCV: none -> 1");

            // composite with default heights (MHID 0, no MTXP: H = 0 * 0 + 1 = 1 on this 0x80 map): the cutoff alone
            uint[] mdid = { 1000, 1001, 1002, 1003, 1004, 0 };
            var tex = new Dictionary<uint, TerrainTexels>
            {
                { 1000, TerrainTexels.Constant(1f, 0f, 0f) }, { 1001, TerrainTexels.Constant(0f, 1f, 0f) }, { 1002, TerrainTexels.Constant(0f, 0f, 1f) },
                { 1003, TerrainTexels.Constant(0.5f, 0.25f, 0.125f) }, { 1004, TerrainTexels.Constant(0.2f, 0.4f, 0.6f) },
            };
            Func<uint, TerrainTexels> lookup = id => tex.TryGetValue(id, out TerrainTexels x) ? x : null;
            byte[] doubled = new byte[580];
            for (int k = 0; k < 145; k++) { doubled[4 * k] = 254; doubled[4 * k + 1] = 254; doubled[4 * k + 2] = 254; doubled[4 * k + 3] = 255; }
            TerrainTileMaterial t = BuildTile(new Tex0Spec
            {
                Mdid = mdid,
                Chunk = n => n == 0 ? TexChunkSpec.Of(Layer(0), Layer(1, alpha: Constant(51)))
                           : n == 1 ? TexChunkSpec.Of(Layer(0), Layer(1, alpha: Constant(10)), Layer(2, alpha: Constant(20)), Layer(3, alpha: Constant(30)), Layer(4, alpha: Constant(40)))
                           : n == 2 ? TexChunkSpec.Of(Layer(0), Layer(1, alpha: Constant(51)))
                           : n == 3 ? TexChunkSpec.Of(Layer(5))
                           : n == 4 ? TexChunkSpec.Of(Layer(0), Layer(5, alpha: Constant(255)))
                           : n == 6 ? TexChunkSpec.Of(Layer(0), Layer(1, alpha: Constant(85)), Layer(2, alpha: Constant(85)))
                           : TexChunkSpec.Of(Layer(0)),
            }, Mphd8, n => n == 2 ? new RootChunkSpec { Flags = RootFlags, VertexColorBytes = doubled } : Rs());
            float cr, cg, cb;
            TerrainCompositor.Compose(t.Chunks[0], lookup, 3.3f, 1.7f, false, out cr, out cg, out cb);
            Check(Near(cr, 0.909090909) && Near(cg, 0.090909091) && cb == 0f,
                  "height mix, H = 1: w = (0.8, 0.2) -> p = (0.8, 0.2 * (1 - 0.6)) = (0.8, 0.08), / 0.88 = (0.909091, 0.090909)", cr + " " + cg + " " + cb);
            TerrainCompositor.Compose(t.Chunks[0], lookup, 3.3f, 1.7f, false, TerrainBlendRule.PlainWeights, out cr, out cg, out cb);
            Check(Near(cr, 0.8) && Near(cg, 0.2) && cb == 0f, "plain weights (the labelled alternative): (0.8, 0.2, 0)", cr + " " + cg + " " + cb);
            TerrainCompositor.Compose(t.Chunks[1], lookup, 5.1f, 0.2f, false, out cr, out cg, out cb);
            Check(Near(cr, 0.810947168) && Near(cg, 0.082151356) && Near(cb, 0.118943360),
                  "height mix, five layers (alpha 10/20/30/40): weights (0.752499, 0.020942, 0.045693, 0.074250, 0.106616) x texels (hand values)", cr + " " + cg + " " + cb);
            TerrainCompositor.Compose(t.Chunks[6], lookup, 2f, 2f, false, out cr, out cg, out cb);
            Check(Near(cr, 1 / 3.0) && Near(cg, 1 / 3.0) && Near(cb, 1 / 3.0), "height mix, three-way tie at the maximum (alpha 85, 85): 1/3 each", cr + " " + cg + " " + cb);
            TerrainCompositor.Compose(t.Chunks[2], lookup, 1f, 1f, false, out cr, out cg, out cb);
            float sr, sg, sb;
            TerrainCompositor.Compose(t.Chunks[2], lookup, 1f, 1f, true, out sr, out sg, out sb);
            Check(Near(cr, 1.818181818) && Near(cg, 0.181818182) && sr == 1f && Near(sg, 0.181818182) && cb == 0f,
                  "composite x MCCV 254/127 = 2 after the height mix: unclamped 1.818182, saturated 1.0", cr + " " + cg + " / " + sr + " " + sg);

            // missing texture checker, one square per 1/8 cell, magenta where floor(8u) + floor(8v) is even
            TerrainCompositor.Compose(t.Chunks[3], lookup, 0.0625f, 0.0625f, false, out cr, out cg, out cb);
            float r2, g2, b2;
            TerrainCompositor.Compose(t.Chunks[3], lookup, 0.1875f, 0.0625f, false, out r2, out g2, out b2);
            float r3, g3, b3;
            TerrainCompositor.Compose(t.Chunks[3], lookup, 7.9f, 0.1f, false, out r3, out g3, out b3);
            Check(t.Chunks[3].State == TerrainMaterialState.MissingTexture && cr == 1f && cg == 0f && cb == 1f && r2 == 0f && g2 == 0f && b2 == 0f &&
                  r3 == 0f && b3 == 0f, "missing texture: magenta/black checker of 1/8-cell squares");
            TerrainCompositor.Compose(t.Chunks[4], lookup, 0.0625f, 0.0625f, false, out cr, out cg, out cb);
            Check(cr == 1f && cg == 0f && cb == 1f && t.Chunks[4].State == TerrainMaterialState.MissingTexture,
                  "missing texture: the checker is weighted like the texture (alpha 255 -> checker only)");

            HeightMixTests(lookup, tex);

            // texel lookup by index and repeats: a 4x4 texture, R = x / 4, G = y / 4
            var rgb = new float[48];
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) { rgb[3 * (y * 4 + x)] = x / 4f; rgb[3 * (y * 4 + x) + 1] = y / 4f; }
            tex[1000] = TerrainTexels.FromRgb(4, 4, rgb);
            TerrainTileMaterial rep = BuildTile(new Tex0Spec
            {
                Mdid = new uint[] { 1000, 1001 },
                Mtxp = Cat(MtxpEntry(0x00), MtxpEntry(0x10)),
                Chunk = n => n == 0 ? TexChunkSpec.Of(Layer(0)) : TexChunkSpec.Of(Layer(1)),
            });
            tex[1001] = tex[1000];
            var probes = new[]
            {
                new[] { 0.125f, 0.875f, 0, 0, 3 }, new[] { 0.375f, 0.1f, 0, 1, 0 }, new[] { 1.125f, 0f, 0, 0, 0 }, new[] { 7.99f, 7.99f, 0, 3, 3 },
                new[] { 1.125f, 0.125f, 1, 2, 0 }, new[] { 2.125f, 3.9f, 1, 0, 3 },
            };
            bool texel = true;
            foreach (float[] p in probes)
            {
                TerrainCompositor.Compose(rep.Chunks[(int)p[2]], lookup, p[0], p[1], false, out cr, out cg, out cb);
                texel &= cr == p[3] / 4f && cg == p[4] / 4f;
            }
            Check(texel && rep.Chunks[1].Layers[0].RepeatsPerChunk == 4, "layer texels: index floor(s * size) mod size at s = uv * repeats / 8 (8 and 4 repeats)");

            // grid: holes cleared, north row first, size
            TerrainTileMaterial gt = BuildTile(new Tex0Spec
            {
                Mdid = new uint[] { 1000, 1001 },
                Chunk = n => TexChunkSpec.Of(Layer(0), Layer(1, alpha: AlphaMapOf((r, c) => 4 * r))),
            }, Mphd8, n => new RootChunkSpec { Flags = RootFlags, Holes = 1UL | (1UL << 63) });
            tex[1000] = TerrainTexels.Constant(0f, 0f, 0f);
            tex[1001] = TerrainTexels.Constant(1f, 1f, 1f);
            float[] grid = TerrainCompositor.ComposeGrid(gt.Chunks[5], lookup, 16, false);
            Check(grid.Length == 16 * 16 * 4 && grid[3] == 0f && grid[0] == 0f && grid[4 * (15 * 16 + 15) + 3] == 0f && grid[4 * 15 + 3] == 1f &&
                  grid[4 * (15 * 16) + 3] == 1f, "grid: hole cells (0, 0) and (7, 7) cleared to alpha 0, others alpha 1");
            float north = grid[4 * 2], south = grid[4 * (15 * 16 + 2)];
            float[] plainGrid = TerrainCompositor.ComposeGrid(gt.Chunks[5], lookup, 16, false, TerrainBlendRule.PlainWeights);
            Check(north < south && Near(north, 0.001132663, 1e-6) && Near(plainGrid[4 * 2], 6 / 255.0, 1e-6),
                  "grid: row 0 is the north edge (small alpha rows), pixel centres at (i + 0.5) * 8 / size; alpha 6/255 there -> height mix 0.0011327, plain 6/255",
                  north + " < " + south + "; plain " + plainGrid[4 * 2]);
            Throws<ArgumentException>(() => TerrainCompositor.Compose(t.Chunks[200], id => null, 1, 1, false, out cr, out cg, out cb), "composite: a missing texture source is an error", "no texels");
            TerrainTileMaterial four = BuildTile(new Tex0Spec { Chunk = n => TexChunkSpec.Of(Layer(0), Layer(1, AlphaStorage.Uncompressed4, Constant(3))) }, Mphd4);
            Throws<ArgumentException>(() => TerrainCompositor.Compose(four.Chunks[0], lookup, 1, 1, false, out cr, out cg, out cb), "composite: an alphaUnsupported chunk has none", "alphaUnsupported");
        }

        /// <summary>The height mix with height textures (v3 delta, section 1), hand values from the formula:
        /// H on and off by MPHD 0x80, ties at the maximum, S == 0, MHID 0 on a 0x80 map, a single layer, the
        /// height texture's ALPHA channel at the layer's UV with the layer's repeats; H below 0 (S < 0, mixed
        /// signs) and a float32 overflow make the chunk layerEffectUnresolved on a 0x80 map and nothing on a
        /// 0x80-clear map.</summary>
        static void HeightMixTests(Func<uint, TerrainTexels> lookup, Dictionary<uint, TerrainTexels> tex)
        {
            float[] res = new float[3];
            bool ok = TerrainCompositor.HeightMixWeights(new[] { 0.5f, 0.3f, 0.2f }, new[] { 1f, 2f, 3f }, 3, res);
            Check(ok && Near(res[0], 0.272727273) && Near(res[1], 0.363636364) && Near(res[2], 0.363636364),
                  "HeightMixWeights: w (0.5, 0.3, 0.2), H (1, 2, 3) -> p (0.5, 0.6, 0.6), m 0.6, p0 * 0.9 -> (0.272727, 0.363636, 0.363636)",
                  string.Join(" ", res));
            ok = TerrainCompositor.HeightMixWeights(new[] { 0.8f, 0.2f }, new[] { 0f, 0f }, 2, res);
            Check(!ok && res[0] == 0.8f && res[1] == 0.2f, "HeightMixWeights: S == 0 -> false and the plain weights", string.Join(" ", res));
            ok = TerrainCompositor.HeightMixWeights(new[] { -1e-7f, 1f }, new[] { 5f, 1f }, 2, res);
            Check(ok && res[0] == 0f && res[1] == 1f, "HeightMixWeights: a weight below 0 (float rounding) counts as 0", string.Join(" ", res));
            Throws<ArgumentException>(() => TerrainCompositor.HeightMixWeights(new[] { 1f }, new[] { 1f }, 2, res), "HeightMixWeights: n beyond the arrays is rejected");

            // entry: MDID texture, MHID height texture, MTXP (flags, height scale, height offset)
            //   0 red (0, 0, 1)           1 green, 2001 (A 0.25), (0, 5, 1)      2 blue, 2002 (2 x 2: A 0, 0.5 / 1, 0.25; RGB 1), (0x10: 4 repeats, 4, 0.5)
            //   3 1003, 2003 (A 0.75), (0, 4, 1)   4 red (0, 0, 0)   5 green (0, 0, -1)   6 blue (0, 3, 0.5)   7 green (0, 0, 0)   8 red (0, 0, -1)
            //   9 red (0, 0, 0.3)          10 green, 2010 (A 1), (0, 3e38, 3e38)
            uint[] mdid = { 1000, 1001, 1002, 1003, 1000, 1001, 1002, 1001, 1000, 1000, 1001 };
            uint[] mhid = { 0, 2001, 2002, 2003, 0, 0, 0, 0, 0, 0, 2010 };
            byte[] mtxp = Cat(MtxpEntry(0), MtxpEntry(0, 5f, 1f), MtxpEntry(0x10, 4f, 0.5f), MtxpEntry(0, 4f, 1f), MtxpEntry(0, 0f, 0f), MtxpEntry(0, 0f, -1f),
                              MtxpEntry(0, 3f, 0.5f), MtxpEntry(0, 0f, 0f), MtxpEntry(0, 0f, -1f), MtxpEntry(0, 0f, 0.3f), MtxpEntry(0, 3e38f, 3e38f));
            tex[1000] = TerrainTexels.Constant(1f, 0f, 0f);
            tex[1001] = TerrainTexels.Constant(0f, 1f, 0f);
            tex[1002] = TerrainTexels.Constant(0f, 0f, 1f);
            tex[2001] = TerrainTexels.Constant(0.9f, 0.9f, 0.9f, 0.25f);
            tex[2002] = TerrainTexels.FromRgba(2, 2, new[] { 1f, 1f, 1f, 0f, 1f, 1f, 1f, 0.5f, 1f, 1f, 1f, 1f, 1f, 1f, 1f, 0.25f });
            tex[2003] = TerrainTexels.Constant(1f, 1f, 1f, 0.75f);
            tex[2010] = TerrainTexels.Constant(1f, 1f, 1f, 1f);
            Func<int, TexChunkSpec> chunks = n => n == 0 ? TexChunkSpec.Of(Layer(0), Layer(1, alpha: Constant(51)))
                                                : n == 1 ? TexChunkSpec.Of(Layer(0), Layer(3, alpha: Constant(51)))
                                                : n == 2 ? TexChunkSpec.Of(Layer(4), Layer(7, alpha: Constant(51)))
                                                : n == 3 ? TexChunkSpec.Of(Layer(8), Layer(5, alpha: Constant(51)))
                                                : n == 4 ? TexChunkSpec.Of(Layer(8), Layer(1, alpha: Constant(51)))
                                                : n == 5 ? TexChunkSpec.Of(Layer(0), Layer(6, alpha: Constant(51)))
                                                : n == 6 ? TexChunkSpec.Of(Layer(1))
                                                : n == 7 ? TexChunkSpec.Of(Layer(4))
                                                : n == 8 ? TexChunkSpec.Of(Layer(0), Layer(2, alpha: Constant(51)))
                                                : n == 9 ? TexChunkSpec.Of(Layer(0), Layer(1, alpha: Constant(51), extraFlags: 0x40))
                                                : n == 10 ? TexChunkSpec.Of(Layer(9), Layer(5, alpha: Constant(128)))
                                                : n == 11 ? TexChunkSpec.Of(Layer(0), Layer(10, alpha: Constant(51)))
                                                : TexChunkSpec.Of(Layer(0));
            var spec = new Tex0Spec { Mdid = mdid, Mhid = mhid, Mtxp = mtxp, Chunk = chunks };
            TerrainTileMaterial on = BuildTile(spec, Mphd8), off = BuildTile(spec, Mphd8NoHeight);
            Check(on.HeightTextureIds.SequenceEqual(new uint[] { 2001, 2003, 2002 }) && off.HeightTextureIds.Length == 0 &&
                  on.Chunks[0].NeedsHeightTextures && !off.Chunks[0].NeedsHeightTextures && on.Chunks[9].State == TerrainMaterialState.LayerEffectStatic,
                  "height-mix tile: height ids [2001, 2003, 2002] with MPHD 0x80, none without", string.Join(" ", on.HeightTextureIds));

            Action<string, TerrainTileMaterial, int, float, float, double, double, double> at = (name, m, n, u, v, er, eg, eb) =>
            {
                float r, g, b;
                TerrainCompositor.Compose(m.Chunks[n], lookup, u, v, false, out r, out g, out b);
                bool good = Near(r, er) && Near(g, eg) && Near(b, eb);
                Check(good, "height mix: " + name, string.Format(System.Globalization.CultureInfo.InvariantCulture, "got ({0:R}, {1:R}, {2:R}), want ({3}, {4}, {5})", r, g, b, er, eg, eb));
            };
            at("H on, h 0.25 x 5 + 1 = 2.25: p (0.8, 0.45 * 0.65) -> (0.732265, 0.267735)", on, 0, 3.3f, 1.7f, 0.732265446, 0.267734554, 0);
            at("H off (MPHD 0x80 clear): the same layers -> (0.909091, 0.090909); MHID/MTXP ignored", off, 0, 3.3f, 1.7f, 0.909090909, 0.090909091, 0);
            at("tie at the maximum: h 0.75 x 4 + 1 = 4 -> p (0.8, 0.8) -> (0.5, 0.5) of red and (0.5, 0.25, 0.125)", on, 1, 6.5f, 0.25f, 0.75, 0.125, 0.0625);
            at("S == 0: H = 0 x 0 + 0 on both layers -> plain weights (0.8, 0.2) of red and green", on, 2, 1f, 1f, 0.8, 0.2, 0);
            // H below 0 or a float32 overflow is outside the domain where the mix is defined: such chunks are
            // layerEffectUnresolved on the 0x80 map (not drawn); on the 0x80-clear map the values are ignored (H = 1)
            const string domain = "MTXP height values let H = h*s + o fall below 0 or overflow float32; the height mix is not defined there";
            Func<TerrainChunkMaterial, bool> outside = c => c.State == TerrainMaterialState.LayerEffectUnresolved && !c.Drawable && c.PlainDiagnostic &&
                                                            c.Alpha != null && c.Reason.Contains(domain);
            Check(outside(on.Chunks[3]) && on.Chunks[3].Reason.StartsWith("layer 0 (texture 1000): MTXP entry 8", StringComparison.Ordinal) &&
                  on.Chunks[3].Reason.Contains("s = 0 and offset o = -1"),
                  "S < 0 (H = -1 on both layers, MTXP offset -1): layerEffectUnresolved; the reason names the layer, s and o", on.Chunks[3].Reason);
            Check(outside(on.Chunks[4]), "mixed sign H (-1, 2.25): layerEffectUnresolved (offset -1)", on.Chunks[4].Reason);
            ok = TerrainCompositor.HeightMixWeights(new[] { 0.5f, 0.5f }, new[] { 0.3f, -1f }, 2, res);
            Check(!ok && res[0] == 0.5f && res[1] == 0.5f,
                  "HeightMixWeights: H (0.3, -1), w (0.5, 0.5) -> p (0.15, -0.5), m 0.15, p1 * 0.35 -> S = 0.15 - 0.175 = -0.025 < 0 although H_0 > 0 and w_0 > 0",
                  string.Join(" ", res));
            Check(outside(on.Chunks[10]) && on.Chunks[10].Reason.StartsWith("layer 1 (texture 1001): MTXP entry 5", StringComparison.Ordinal),
                  "mixed sign H (0.3, -1) at a = 128/255 (S < 0 there): layerEffectUnresolved", on.Chunks[10].Reason);
            Check(outside(on.Chunks[11]) && on.Chunks[11].Reason.Contains("s = 3E+38 and offset o = 3E+38 (height texture 2010)"),
                  "finite overflow: MTXP (3e38, 3e38) with a height texture: layerEffectUnresolved (s + o is +infinity in float32)", on.Chunks[11].Reason);
            float hInf = TerrainCompositor.HeightAt(on.Chunks[11], on.Chunks[11].Layers[1], lookup, 1f, 1f);
            Check(float.IsPositiveInfinity(hInf), "finite overflow: with h = 1 the stored values give H = 3e38 * 1 + 3e38 = +infinity in float32",
                  hInf.ToString("R", System.Globalization.CultureInfo.InvariantCulture));
            Check(!on.HeightTextureIds.Contains(2010u) && new[] { 3, 4, 10, 11 }.All(n => off.Chunks[n].State == TerrainMaterialState.Textured) &&
                  TerrainCompositor.HeightAt(off.Chunks[11], off.Chunks[11].Layers[1], lookup, 1f, 1f) == 1f,
                  "MPHD 0x80 clear: the same chunks ignore the height values (H = 1): textured, no state; no height texture requested for the unresolved chunk",
                  States(off, 3, 4, 10, 11));
            at("H off: offsets -1 ignored, H = 1 -> (0.909091, 0.090909) of red and green", off, 3, 1f, 1f, 0.909090909, 0.090909091, 0);
            at("MHID 0 on an 0x80 map: H = 0 x 3 + 0.5 = 0.5 -> p (0.8, 0.1 * 0.3) -> (0.963855, 0.036145) of red and blue", on, 5, 2f, 5f, 0.963855422, 0, 0.036144578);
            at("single layer, H 2.25: weight 1 -> green", on, 6, 4f, 4f, 0, 1, 0);
            at("single layer, H 0 (S == 0): the plain weight 1 -> red", on, 7, 4f, 4f, 1, 0, 0);
            at("layerEffectStatic (MCLY 0x40) is drawn with the same static composite", on, 9, 3.3f, 1.7f, 0.732265446, 0.267734554, 0);
            // the height texture of entry 2: 2 x 2 texels, 4 repeats per chunk: texel (floor(u) mod 2, floor(v) mod 2), its ALPHA
            at("height texel (0, 0) A 0: H 0.5 -> (0.963855, 0.036145) of red/blue", on, 8, 0.5f, 0.5f, 0.963855422, 0, 0.036144578);
            at("height texel (1, 0) A 0.5: H 2.5 -> (0.695652, 0.304348)", on, 8, 1.5f, 0.5f, 0.695652174, 0, 0.304347826);
            at("height texel (0, 1) A 1: H 4.5 -> p (0.72, 0.9) -> (0.444444, 0.555556)", on, 8, 0.5f, 1.5f, 0.444444444, 0, 0.555555556);
            at("height texel (1, 1) A 0.25: H 1.5 -> (0.842105, 0.157895)", on, 8, 1.5f, 1.5f, 0.842105263, 0, 0.157894737);
            at("height texel wraps: u 2.5, v 3.5 -> texel (0, 1) again", on, 8, 2.5f, 3.5f, 0.444444444, 0, 0.555555556);
            at("H off: the same chunk ignores the height texture -> (0.909091, 0.090909)", off, 8, 0.5f, 1.5f, 0.909090909, 0, 0.090909091);
            float hr = TerrainCompositor.HeightAt(on.Chunks[8], on.Chunks[8].Layers[1], lookup, 1.5f, 0.5f);
            float h0 = TerrainCompositor.HeightAt(on.Chunks[8], on.Chunks[8].Layers[0], lookup, 1.5f, 0.5f);
            float hoff = TerrainCompositor.HeightAt(off.Chunks[8], off.Chunks[8].Layers[1], lookup, 1.5f, 0.5f);
            Check(hr == 2.5f && h0 == 1f && hoff == 1f, "HeightAt: h * scale + offset from the height texture's alpha (0.5 * 4 + 0.5); MHID 0 -> offset; 0x80 clear -> 1",
                  hr + " " + h0 + " " + hoff);

            float cr, cg, cb;
            TerrainCompositor.Compose(on.Chunks[0], lookup, 1f, 1f, false, TerrainBlendRule.PlainWeights, out cr, out cg, out cb);
            Check(Near(cr, 0.8) && Near(cg, 0.2), "plain weights ignore heights and the cutoff (labelled alternative)", cr + " " + cg);
            Throws<ArgumentException>(() => TerrainCompositor.Compose(on.Chunks[0], id => id == 2001 ? null : lookup(id), 1, 1, false, out cr, out cg, out cb),
                                      "height mix: a missing height texture source is an error", "no texels for height texture 2001");
            Throws<ArgumentException>(() => TerrainCompositor.Compose(on.Chunks[0], id => id == 2001 ? TerrainTexels.Constant(1f, 1f, 1f) : lookup(id), 1, 1, false, out cr, out cg, out cb),
                                      "height mix: height texels without alpha are an error (the height is the alpha channel)", "no alpha texels");
            on.ApplyHeightTextureFailures(new uint[] { 2001 });
            Check(on.Chunks[0].State == TerrainMaterialState.HeightTextureMissing && on.Chunks[4].State == TerrainMaterialState.LayerEffectUnresolved &&
                  on.Chunks[6].State == TerrainMaterialState.HeightTextureMissing &&
                  on.Chunks[9].State == TerrainMaterialState.HeightTextureMissing && on.Chunks[8].State == TerrainMaterialState.Textured,
                  "a failed height texture: every drawable chunk reading it is heightTextureMissing (chunk 4, layerEffectUnresolved, keeps its state)",
                  States(on, 0, 4, 6, 8, 9));
            Throws<ArgumentException>(() => TerrainCompositor.Compose(on.Chunks[0], lookup, 1, 1, false, out cr, out cg, out cb),
                                      "height mix: a heightTextureMissing chunk has no height-mix composite", "heightTextureMissing");
            TerrainCompositor.Compose(on.Chunks[0], lookup, 1f, 1f, false, TerrainBlendRule.PlainWeights, out cr, out cg, out cb);
            Check(Near(cr, 0.8) && Near(cg, 0.2), "plain weights still draw a heightTextureMissing chunk (diagnostic view)", cr + " " + cg);
        }

        // ================================================================================== BLP header

        /// <summary>A BLP2 file: header, then each level's bytes back to back (filled with the level number).</summary>
        static byte[] Blp(int encoding, int alphaDepth, int preferred, int w, int h, int levels, Func<int, int> size, int hasMips = 2)
        {
            var sizes = new int[16];
            int total = 0x494;
            for (int m = 0; m < levels; m++) { sizes[m] = size(m); total += sizes[m]; }
            var b = new byte[total];
            b[0] = (byte)'B'; b[1] = (byte)'L'; b[2] = (byte)'P'; b[3] = (byte)'2';
            PutU32(b, 4, 1);
            b[8] = (byte)encoding; b[9] = (byte)alphaDepth; b[10] = (byte)preferred; b[11] = (byte)hasMips;
            PutU32(b, 12, (uint)w); PutU32(b, 16, (uint)h);
            int off = 0x494;
            for (int m = 0; m < levels; m++)
            {
                PutU32(b, 0x14 + 4 * m, (uint)off);
                PutU32(b, 0x54 + 4 * m, (uint)sizes[m]);
                for (int i = 0; i < sizes[m]; i++) b[off + i] = (byte)m;
                off += sizes[m];
            }
            return b;
        }

        static int DxtBytes(int w, int h, int m, int block) { return Math.Max(1, ((w >> m) + 3) / 4) * Math.Max(1, ((h >> m) + 3) / 4) * block; }

        static void BlpHeaderTests()
        {
            foreach (var f in new[] { new { P = 0, A = 0, F = BlpPixelFormat.Dxt1, B = 8 }, new { P = 1, A = 8, F = BlpPixelFormat.Dxt3, B = 16 },
                                      new { P = 7, A = 8, F = BlpPixelFormat.Dxt5, B = 16 }, new { P = 1, A = 1, F = BlpPixelFormat.Dxt1, B = 8 } })
            {
                byte[] file = Blp(2, f.A, f.P, 256, 256, 9, m => DxtBytes(256, 256, m, f.B));
                BlpHeader h = BlpDecoder.ReadHeader(file);
                bool ok = h.Format == f.F && h.BlockBytes == f.B && h.IsBlockCompressed && h.Width == 256 && h.Height == 256 && h.AlphaDepth == f.A &&
                          h.PreferredFormat == f.P && h.MipCount == 9 && h.FullChainLength == 9 && h.HasMipsRaw == 2 && h.Mips[0].Offset == 0x494;
                int off = 0x494;
                for (int m = 0; m < 9; m++)
                {
                    ok &= h.Mips[m].Level == m && h.Mips[m].Offset == off && h.Mips[m].StoredSize == h.Mips[m].DataSize &&
                          h.Mips[m].DataSize == DxtBytes(256, 256, m, f.B) && h.Mips[m].Width == 256 >> m && h.Mips[m].Height == 256 >> m;
                    off += h.Mips[m].StoredSize;
                    BlpImage img = BlpDecoder.Decode(file, m);
                    ok &= img.Width == h.Mips[m].Width && img.Height == h.Mips[m].Height && img.Encoding == h.Encoding;
                }
                Check(ok, string.Format("BLP header: {0} (preferredFormat {1}, alpha {2}), 256x256, 9 levels, offsets and block sizes; Decode agrees per level", h.Encoding, f.P, f.A));
            }
            BlpHeader rect = BlpDecoder.ReadHeader(Blp(2, 8, 7, 8, 4, 4, m => DxtBytes(8, 4, m, 16)));
            Check(rect.MipCount == 4 && rect.Mips.Select(m => m.Width + "x" + m.Height).SequenceEqual(new[] { "8x4", "4x2", "2x1", "1x1" }) &&
                  rect.Mips.Select(m => m.DataSize).SequenceEqual(new[] { 32, 16, 16, 16 }), "BLP header: 8x4 chain to 1x1, one block minimum per level");
            BlpHeader shortChain = BlpDecoder.ReadHeader(Blp(2, 0, 0, 64, 64, 3, m => DxtBytes(64, 64, m, 8)));
            Check(shortChain.MipCount == 3 && shortChain.FullChainLength == 7, "BLP header: stored levels end at the first zero size");
            byte[] extra = Blp(2, 0, 0, 1, 1, 3, m => 8);
            Check(BlpDecoder.ReadHeader(extra).MipCount == 1, "BLP header: levels beyond the 1x1 level are not counted");
            BlpHeader pal = BlpDecoder.ReadHeader(Blp(1, 8, 0, 16, 8, 5, m => Math.Max(1, 16 >> m) * Math.Max(1, 8 >> m) * 2));
            BlpHeader raw = BlpDecoder.ReadHeader(Blp(3, 8, 0, 4, 4, 3, m => Math.Max(1, 4 >> m) * Math.Max(1, 4 >> m) * 4));
            Check(pal.Format == BlpPixelFormat.Palettized && pal.Encoding == "palettized/a8" && pal.Mips[0].DataSize == 256 && pal.Mips[4].DataSize == 2 &&
                  !pal.IsBlockCompressed && pal.BlockBytes == 0 && raw.Format == BlpPixelFormat.RawBgra && raw.Mips[1].DataSize == 16 && raw.Encoding == "bgra8888",
                  "BLP header: palettized (indices + 8-bit alpha) and raw BGRA level sizes");

            Throws<WowParseException>(() => BlpDecoder.ReadHeader(null), "BLP header: null rejected");
            Throws<WowParseException>(() => BlpDecoder.ReadHeader(new byte[100]), "BLP header: short file rejected", "smaller than");
            byte[] magic = Blp(2, 0, 0, 4, 4, 1, m => 8); magic[3] = (byte)'1';
            Throws<WowParseException>(() => BlpDecoder.ReadHeader(magic), "BLP header: wrong magic rejected", "BLP2");
            byte[] ver = Blp(2, 0, 0, 4, 4, 1, m => 8); PutU32(ver, 4, 2);
            Throws<WowParseException>(() => BlpDecoder.ReadHeader(ver), "BLP header: version 2 rejected", "version");
            Throws<WowParseException>(() => BlpDecoder.ReadHeader(Blp(2, 0, 0, 0, 4, 1, m => 8)), "BLP header: zero width rejected", "dimensions");
            Throws<WowParseException>(() => BlpDecoder.ReadHeader(Blp(4, 0, 0, 4, 4, 1, m => 8)), "BLP header: colorEncoding 4 rejected", "colorEncoding 4");
            Throws<WowParseException>(() => BlpDecoder.ReadHeader(Blp(2, 0, 0, 4, 4, 0, m => 8)), "BLP header: no level 0 rejected", "level 0");
            byte[] past = Blp(2, 0, 0, 8, 8, 4, m => DxtBytes(8, 8, m, 8)); PutU32(past, 0x14 + 4 * 2, (uint)past.Length - 4);
            Throws<WowParseException>(() => BlpDecoder.ReadHeader(past), "BLP header: a level outside the file rejected", "outside");
            Throws<WowParseException>(() => BlpDecoder.ReadHeader(Blp(2, 8, 7, 8, 8, 1, m => 16)), "BLP header: a DXT level smaller than its blocks rejected", "needs 64");
        }

        // ================================================================================== BLP exact blocks

        /// <summary>A one-level BLP2 with colorEncoding 2 of w x h whose level 0 is the given blocks.</summary>
        static byte[] DxtFile(int alphaDepth, int preferred, int w, int h, params byte[][] blocks)
        {
            byte[] data = Cat(blocks);
            byte[] b = Blp(2, alphaDepth, preferred, w, h, 1, m => data.Length);
            Array.Copy(data, 0, b, 0x494, data.Length);
            return b;
        }

        /// <summary>Pixels x0 .. x0 + count - 1 of row y as "r,g,b,a" separated by spaces.</summary>
        static string Row(BlpImage img, int y, int x0, int count)
        {
            var s = new List<string>();
            for (int x = x0; x < x0 + count; x++)
            {
                int o = 4 * (y * img.Width + x);
                s.Add(img.Rgba[o] + "," + img.Rgba[o + 1] + "," + img.Rgba[o + 2] + "," + img.Rgba[o + 3]);
            }
            return string.Join(" ", s);
        }

        /// <summary>The 6 index bytes of a DXT5 alpha block in which texel i selects ramp entry i % 8.</summary>
        static byte[] AlphaIndexRamp()
        {
            ulong bits = 0;
            for (int i = 0; i < 16; i++) bits |= (ulong)(i % 8) << (3 * i);
            var b = new byte[6];
            for (int i = 0; i < 6; i++) b[i] = (byte)(bits >> (8 * i));
            return b;
        }

        /// <summary>BlpDecoder.Decode's opt-in exact-blocks mode against hand values (bit-replicated 565, colour
        /// interpolants and DXT5 alpha ramps rounded to nearest from the unrounded value, the DXT1 three-colour
        /// midpoint with ties to even, index 3 = (0, 0, 0, 0), DXT3 nibble * 17, DXT3/DXT5 colour always four-colour),
        /// and the default mode pinned to its unchanged truncating values.</summary>
        static void BlpExactBlockTests()
        {
            // c0 0xEE43 (r 29, g 50, b 3) and c1 0x2151 (r 4, g 10, b 17): replicated (239, 203, 24) and (33, 40, 140),
            // truncated v * 255 / 31, v * 255 / 63: (238, 202, 24) and (32, 40, 139). Index bytes 0xE4: each row selects 0, 1, 2, 3.
            byte[] fourColour = { 0x43, 0xEE, 0x51, 0x21, 0xE4, 0xE4, 0xE4, 0xE4 };
            byte[] threeColour = { 0x51, 0x21, 0x43, 0xEE, 0xE4, 0xE4, 0xE4, 0xE4 };   // c0 0x2151 <= c1 0xEE43
            byte[] tieDown = { 0x00, 0x08, 0x00, 0x20, 0xAA, 0xAA, 0xAA, 0xAA };       // c0 0x0800 (r 8) <= c1 0x2000 (r 33); index 2 everywhere
            byte[] dxt1 = DxtFile(0, 0, 12, 4, fourColour, threeColour, tieDown);
            BlpImage ex = BlpDecoder.Decode(dxt1, 0, true), df = BlpDecoder.Decode(dxt1, 0), dfFalse = BlpDecoder.Decode(dxt1, 0, false);
            const string four = "239,203,24,255 33,40,140,255 170,149,63,255 102,94,101,255";
            const string three = "33,40,140,255 239,203,24,255 136,122,82,255 0,0,0,0";
            const string tie = "20,0,0,255 20,0,0,255 20,0,0,255 20,0,0,255";
            Check(Row(ex, 0, 0, 4) == four && Row(ex, 3, 0, 4) == four && ex.Encoding == "dxt1",
                  "BLP exact blocks, DXT1 four-colour: endpoints by bit replication; (2a + b) / 3 = (170.33, 148.67, 62.67) and (a + 2b) / 3 = (101.67, 94.33, 101.33) rounded to nearest",
                  Row(ex, 0, 0, 4));
            Check(Row(ex, 0, 4, 4) == three && Row(ex, 3, 4, 4) == three,
                  "BLP exact blocks, DXT1 three-colour: midpoint (136, 121.5, 82) -> (136, 122, 82) (tie to even), index 3 = (0, 0, 0, 0)", Row(ex, 0, 4, 4));
            Check(Row(ex, 0, 8, 4) == tie && Row(ex, 2, 8, 4) == tie, "BLP exact blocks, DXT1 three-colour: midpoint (8 + 33) / 2 = 20.5 -> 20 (tie to even)", Row(ex, 0, 8, 4));
            Check(Row(df, 0, 0, 12) == "238,202,24,255 32,40,139,255 169,148,62,255 100,94,100,255 32,40,139,255 238,202,24,255 135,121,81,255 0,0,0,0 " + tie &&
                  df.Rgba.SequenceEqual(dfFalse.Rgba),
                  "BLP default decode unchanged (models and world models): endpoints v * 255 / 31 and v * 255 / 63, interpolants truncated", Row(df, 0, 0, 12));

            // DXT3: alpha nibble i at texel i -> 17 i; colour block four-colour
            byte[] dxt3 = DxtFile(8, 1, 4, 4, Cat(new byte[] { 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE }, fourColour));
            BlpImage ex3 = BlpDecoder.Decode(dxt3, 0, true), df3 = BlpDecoder.Decode(dxt3, 0);
            bool alpha3 = true;
            for (int i = 0; i < 16; i++) alpha3 &= ex3.Rgba[4 * i + 3] == 17 * i && df3.Rgba[4 * i + 3] == 17 * i;
            Check(alpha3 && ex3.Encoding == "dxt3" && Row(ex3, 0, 0, 4) == "239,203,24,0 33,40,140,17 170,149,63,34 102,94,101,51" &&
                  Row(df3, 3, 0, 4) == "238,202,24,204 32,40,139,221 169,148,62,238 100,94,100,255",
                  "BLP exact blocks, DXT3: alpha nibble * 17 in both modes; colour exact vs truncated", Row(ex3, 0, 0, 4) + " | " + Row(df3, 3, 0, 4));

            // DXT5: block 1 a0 200 > a1 13 (eight values), block 2 a0 13 <= a1 200 (six values, 0, 255); texel i selects i % 8.
            // The colour block has c0 < c1 and is still four-colour.
            byte[] idx = AlphaIndexRamp();
            byte[] dxt5 = DxtFile(8, 7, 8, 4, Cat(new byte[] { 200, 13 }, idx, threeColour), Cat(new byte[] { 13, 200 }, idx, threeColour));
            BlpImage ex5 = BlpDecoder.Decode(dxt5, 0, true), df5 = BlpDecoder.Decode(dxt5, 0);
            int[] eight = { 200, 13, 173, 147, 120, 93, 66, 40 }, six = { 13, 200, 50, 88, 125, 163, 0, 255 };
            int[] eightTrunc = { 200, 13, 173, 146, 119, 93, 66, 39 }, sixTrunc = { 13, 200, 50, 87, 125, 162, 0, 255 };
            bool ramp = true;
            var got = new List<int>();
            for (int i = 0; i < 16; i++)
            {
                int x = i % 4, y = i / 4;
                ramp &= ex5.Rgba[4 * (y * 8 + x) + 3] == eight[i % 8] && ex5.Rgba[4 * (y * 8 + 4 + x) + 3] == six[i % 8] &&
                        df5.Rgba[4 * (y * 8 + x) + 3] == eightTrunc[i % 8] && df5.Rgba[4 * (y * 8 + 4 + x) + 3] == sixTrunc[i % 8];
                got.Add(ex5.Rgba[4 * (y * 8 + x) + 3]);
            }
            Check(ramp, "BLP exact blocks, DXT5 alpha: ((6 - i) a0 + (1 + i) a1) / 7 and ((4 - i) a0 + (1 + i) a1) / 5 rounded to nearest (147, 120, 40; 88, 163), " +
                        "the default truncated (146, 119, 39; 87, 162)", string.Join(" ", got));
            string colour5 = "33,40,140 239,203,24 102,94,101 170,149,63";
            Check(ex5.Encoding == "dxt5" && string.Join(" ", Row(ex5, 1, 4, 4).Split(' ').Select(p => p.Substring(0, p.LastIndexOf(',')))) == colour5,
                  "BLP exact blocks, DXT5 colour: c0 < c1 is still four-colour (no midpoint, no transparent black)", Row(ex5, 1, 4, 4));

            // palettized and raw BGRA are exact either way: the flag changes nothing
            byte[] raw = Blp(3, 8, 0, 4, 4, 1, m => 64);
            for (int i = 0; i < 64; i++) raw[0x494 + i] = (byte)(i * 37 + 11);
            byte[] pal = Blp(1, 8, 0, 4, 4, 1, m => 32);
            for (int i = 0; i < 1024; i++) pal[0x94 + i] = (byte)(i * 13);
            for (int i = 0; i < 32; i++) pal[0x494 + i] = (byte)(i * 29);
            Check(BlpDecoder.Decode(raw, 0, true).Rgba.SequenceEqual(BlpDecoder.Decode(raw, 0).Rgba) &&
                  BlpDecoder.Decode(pal, 0, true).Rgba.SequenceEqual(BlpDecoder.Decode(pal, 0).Rgba),
                  "BLP exact blocks: raw BGRA and palettized decode the same with and without the flag");
        }

        // ================================================================================== threads

        static void ThreadTests()
        {
            byte[] rootBytes = RootWith(TX, TY, n => new RootChunkSpec { Flags = RootFlags, VertexColorBytes = MccvBytes() });
            byte[] texBytes = Tex0(new Tex0Spec { Mdid = StateMdid, Mhid = StateMhid, Mtxp = StateMtxp(), Chunk = StateChunk });
            AdtRoot r = AdtParser.ParseRoot(rootBytes, TX, TY);
            AdtTex tx = AdtParser.ParseTex(texBytes);
            TerrainTileMaterial reference = TerrainMaterialBuilder.Build(r, tx, Mphd8);
            var results = new TerrainTileMaterial[8];
            Parallel.For(0, 8, i => results[i] = TerrainMaterialBuilder.Build(r, tx, Mphd8));
            bool same = results.All(m => m.Chunks.Select(c => c.State).SequenceEqual(reference.Chunks.Select(c => c.State)) &&
                                         m.Chunks.All(c => c.Alpha == null ? reference.Chunks[c.Index].Alpha == null : c.Alpha.SequenceEqual(reference.Chunks[c.Index].Alpha)) &&
                                         m.TextureIds.SequenceEqual(reference.TextureIds));
            Check(same, "Build on 8 threads at once from the same parsed tile: identical states, slices and texture lists");
        }

        /// <summary>Run one group of checks; an exception that escapes it is one FAIL (the rest of the group is not run),
        /// so the later groups and suites still run.</summary>
        static void Group(string title, Action run)
        {
            log.Add(title);
            try
            {
                run();
            }
            catch (Exception e)
            {
                log.Add("  FAIL  " + title + ": unexpected " + e.GetType().Name + " (" + e.Message + "); the rest of the group did not run");
                failures++;
            }
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            Group("Alignment, UVs and the synthetic tex0 encoders", AlignmentTests);
            Group("Chunk states one by one (effect rule, MTXP bits), layers, height values, slices, MCCV", StateTests);
            Group("4-bit maps, MPHD (0x84, 0x80), MTXP, the MDID/MHID/MTXP table rules and tile states", FourBitAndTileTests);
            Group("Precedence (as built and with a failed height texture)", PrecedenceTests);
            Group("MTXP scale nibble above 4 and height values outside the height mix's domain (layerEffectUnresolved)", ScaleNibbleAndHeightDomainTests);
            Group("6..8 layers (second alpha slice), nibble 4 (0.5 repeats, the chunk's UV phase), nibble 5 (the checker)", ManyLayersAndNibble4Tests);
            Group("Diffuse and height texture lists, references, slices, texture and height-texture failures", TextureListTests);
            Group("Compositor: the height mix (hand values), plain weights, sampling, MCCV, checker, grid", CompositorTests);
            Group("BLP header and mip table", BlpHeaderTests);
            Group("BLP exact-blocks decode (opt-in) against hand values; the default decode unchanged", BlpExactBlockTests);
            Group("Thread safety", ThreadTests);
            log.Add(string.Format("TerrainMaterialTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }
}
