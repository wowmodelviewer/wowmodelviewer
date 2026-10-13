// TerrainMaterialCorpusTests.cs
//
// The terrain material builder (Assets/Scripts/Wow/World/TerrainMaterial.cs) and the BLP header API on
// REAL client bytes: the pinned tiles and chunks of the stage 04 design (section 7) that exist in the
// audit corpus (WMV_WORLD_AUDIT_CORPUS, the git-ignored build/world-audit-data), plus tiles extracted for
// this stage (materials/<client>/stage04_core, stage04_golden, stage04_nibble4, modern_probe, each with a
// manifest). Runs only from the console runner.
//
//   1. Inputs: every WDT, root and tex0 is checked against Tools/world-audit/tests/corpus_hashes.json (its
//      own pin or its pinned file set) or, outside the corpus, against the SHA-256 pinned below AND the
//      extraction's manifest_sha256.tsv. The root and tex0 FileDataIDs are taken from the WDT's MAID (words
//      0 and 3) and compared with the pins; the MPHD flags are read from the WDT.
//   2. Per tile: the state counters (v3 delta, section 2) and notes, the alpha storage counts, the
//      diffuse texture ids of the drawable chunks in first use, the height texture ids (MPHD 0x80 maps),
//      the plain-only ids, the reference counts, the slice counts (a second slice for each chunk of 6..8
//      layers) and the tile state. The expected values were computed independently from the same bytes
//      with the audit's Python reader (a scratch script implementing the state table with the round-3 rules;
//      no C# output read).
//   3. Per pinned chunk: state, layer FileDataIDs, MCLY flags, storage, and the SHA-256 (first 12 hex
//      digits) of each decoded alpha map -- the census values (stage 04 census, section 12; for Retail map
//      2923 the independent script's) -- taken from the alpha slice channel when the chunk has a slice
//      (layers 5..7 from its second slice) and from the parser's map as well, holes, MCCV, and (where
//      pinned) the layers' height texture ids, repeats and UV phase (0.5 * (ix % 2), 0.5 * (iy % 2) at 0.5
//      repeats, else 0).
//   4. Retail 30_49 (the height-textured tile): its height textures are local (verified, BLP with alpha),
//      the height-textured chunks are Textured, and a failed height texture makes exactly the chunks that
//      read it HeightTextureMissing.
//   5. The composite rule against the game's own baked map texture (MAID word 5) with the compositor
//      itself: every drawable chunk composed at 32 px (each layer and height texture at its own stored
//      mip of one texel per pixel), 4x4-block RMSE over the kept blocks -- the verified height mix
//      against plain weights and against the opposite MPHD 0x80 reading (heights off on a 0x80 map, on
//      on a 0x80-clear map), next to the measurements the design records (v3 delta, section 1); on Retail
//      30_49's height-textured chunks also against heights sampled at 8 repeats instead of the layer's
//      MTXP repeats (strictly worse; the height texture's mip is not separated by any fit and stays
//      INFERRED); on Retail map 2923 32_24 (MTXP scale nibble 4, chunks of 6..8 layers) also against
//      nibble 4 with chunk-local UV, as 1 repeat and as 8 repeats (worse by more than the allowance), on
//      every drawable chunk, the chunks with a nibble-4 layer and the chunks of 6..8 layers; the recorded
//      values there are the Python reference's (the research run and a rerun against the same frozen
//      reference: fix-round-2 research, round-3 core notes).
//   6. Build time per tile over 5 runs (the first includes JIT).
//   7. BLP headers of the extracted terrain textures (census_blp): format as the census lists it, every
//      stored level present and exactly sized, back to back from the header, Decode agreeing per level.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Wmv.Wow.World;

namespace Wmv.Wow.Tests
{
    public static class TerrainMaterialCorpusTests
    {
        const uint AzerothWdt = 775971;

        sealed class ChunkPin
        {
            public int Index;
            public TerrainMaterialState State;
            public uint[] Ids;
            public uint[] Flags;
            public string[] Storage;      // "base", "rle", "raw8", "raw4"
            public string[] Hash12;       // "-" for the base layer
            public ulong Holes;
            public bool VertexColors;
            public uint[] HeightIds;      // per layer (MHID), null = not checked
            public float[] Repeats;       // per layer, null = not checked (then also the UV phase)
        }

        sealed class TileCase
        {
            public string Client, Label;
            public int X, Y;
            public uint Wdt, Root, Tex0, Mphd;
            public string RootDir, TexDir;          // corpus-relative folders
            public string RootSha, TexSha;          // full SHA-256 for files outside the pinned corpus; null = corpus pins
            public string RootSha12, TexSha12;      // census section 12 prefixes (null when the census lists none)
            public int[] Counts;                    // by TerrainMaterialState order
            public int Shadow, Mismatch, VertexColorChunks, Raw8, Rle, Raw4;
            public int MdidEntries;
            public uint[] TextureIds, PlainOnly, HeightIds = new uint[0];
            public int Refs, Zero, Dup, HeightRefs, HeightDup, Notes;
            /// <summary>Alpha slices of the drawable and of the plain-diagnostic chunks; -1 = one per chunk (no chunk of
            /// more than 5 layers).</summary>
            public int Slices = -1, PlainSliceCount = -1;
            public TerrainTileMaterialState TileState;
            public ChunkPin[] Pins = new ChunkPin[0];
        }

        static int[] Counts(int malformed = 0, int alpha = 0, int layers = 0, int unresolved = 0, int heightMissing = 0, int none = 0, int effectStatic = 0,
                            int missing = 0, int textured = 0)
        {
            return new[] { malformed, alpha, layers, unresolved, heightMissing, none, effectStatic, missing, textured };
        }

        /// <summary>"fdid:flags:storage:hash12" per layer (storage "base" and hash "-" for layer 0).</summary>
        static ChunkPin Pin(int index, TerrainMaterialState state, bool mccv, ulong holes, params string[] layers)
        {
            var p = new ChunkPin { Index = index, State = state, VertexColors = mccv, Holes = holes };
            p.Ids = layers.Select(l => uint.Parse(l.Split(':')[0], CultureInfo.InvariantCulture)).ToArray();
            p.Flags = layers.Select(l => Convert.ToUInt32(l.Split(':')[1], 16)).ToArray();
            p.Storage = layers.Select(l => l.Split(':')[2]).ToArray();
            p.Hash12 = layers.Select(l => l.Split(':')[3]).ToArray();
            return p;
        }

        const TerrainMaterialState Tx = TerrainMaterialState.Textured;
        const string Az = "corpus/{0}/azeroth";

        // Design section 7 (pinned tiles present in the corpus) and census section 12 (alpha hashes).
        static readonly TileCase[] Cases =
        {
            new TileCase
            {
                Client = "wow", X = 32, Y = 50, Wdt = AzerothWdt, Root = 778037, Tex0 = 778040, Mphd = 0x3CA, RootSha12 = "db73475ed0f7", TexSha12 = "d3044992c32a",
                Counts = Counts(textured: 256), VertexColorChunks = 0, Raw8 = 1, Rle = 692, MdidEntries = 10,
                TextureIds = new uint[] { 187127, 187138, 187115, 187125, 186770, 187100, 187088, 187082, 187094, 187098 }, PlainOnly = new uint[0],
                Refs = 949, Zero = 0, Dup = 939, TileState = TerrainTileMaterialState.Textured,
                Pins = new[]
                {
                    Pin(87, Tx, false, 0, "187127:0:base:-", "186770:300:rle:2b3d72e4dbad", "187115:300:rle:b78f6d4ba277", "187125:300:rle:0dd9dbfdb042"),
                    Pin(246, Tx, false, 0, "187088:0:base:-", "187082:100:raw8:7a5d7a080e8f"),
                    Pin(129, Tx, false, 0, "187127:0:base:-", "186770:300:rle:affa494747db", "187125:300:rle:c7e3f3f945ca", "187115:300:rle:00d5cb915159"),
                    Pin(47, Tx, false, 0, "187127:0:base:-", "187115:300:rle:f855e24089a9", "186770:300:rle:9c0a40dce68a", "187125:300:rle:283870230b8c"),
                    Pin(253, Tx, false, 0, "187082:0:base:-", "187094:300:rle:fe7cba4e7b64", "187088:300:rle:33818b7a790d"),
                    Pin(150, Tx, false, 0, "187127:0:base:-", "187138:300:rle:63b76ea9f721", "187115:300:rle:f0b5e5442c59", "187125:300:rle:a7502de45340"),
                },
            },
            new TileCase
            {
                Client = "wow", X = 34, Y = 46, Wdt = AzerothWdt, Root = 778417, Tex0 = 778420, Mphd = 0x3CA, RootSha12 = "675050b43840", TexSha12 = "c7889f814179",
                Counts = Counts(effectStatic: 11, textured: 245), VertexColorChunks = 152, Raw8 = 18, Rle = 550, MdidEntries = 9,
                TextureIds = new uint[] { 186934, 186951, 186941, 186947, 252273, 188903, 186936, 186949, 186962 }, PlainOnly = new uint[0],
                Refs = 824, Zero = 0, Dup = 815, TileState = TerrainTileMaterialState.Partial,
                Pins = new[]
                {
                    Pin(48, Tx, true, 0, "186934:0:base:-", "186951:300:rle:a03dae85e4a1", "186947:100:raw8:5c4c5fe69078", "252273:300:rle:2f8ffb156f27"),
                    Pin(146, Tx, true, 0x303, "186934:0:base:-", "186951:300:rle:4dfda64661c4", "188903:300:rle:b1103a5971f0"),
                    Pin(9, Tx, true, 0, "186934:0:base:-", "186951:300:rle:4e840b6ba1b3", "186941:300:rle:1195025bef93"),
                },
            },
            new TileCase
            {
                Client = "wow", X = 29, Y = 50, Wdt = AzerothWdt, Root = 777437, Tex0 = 777440, Mphd = 0x3CA,
                Counts = Counts(textured: 256), VertexColorChunks = 1, Raw8 = 32, Rle = 616, MdidEntries = 13,
                TextureIds = new uint[] { 189004, 189006, 188533, 189008, 187094, 188980, 188992, 188990, 187098, 189014, 188582, 188982, 188984 },
                PlainOnly = new uint[0], Refs = 904, Zero = 0, Dup = 891, TileState = TerrainTileMaterialState.Textured,
            },
            new TileCase
            {
                Client = "wow", X = 32, Y = 48, Wdt = AzerothWdt, Root = 778027, Tex0 = 778030, Mphd = 0x3CA, RootSha12 = "c5fe48f8d1e5", TexSha12 = "29d31204480c",
                Counts = Counts(textured: 256), VertexColorChunks = 57, Raw8 = 3, Rle = 625, MdidEntries = 10,
                TextureIds = new uint[] { 187138, 187115, 187127, 187125, 186770, 321215, 242660, 187100, 187901, 188986 }, PlainOnly = new uint[0],
                Refs = 884, Zero = 0, Dup = 874, TileState = TerrainTileMaterialState.Textured,
                Pins = new[]
                {
                    Pin(158, Tx, true, 0, "187138:0:base:-", "242660:300:rle:c2dfc3ea9be3", "187115:300:rle:d4389ca636fa", "321215:300:rle:b01d8d80119a"),
                    Pin(97, Tx, false, 0, "187138:0:base:-", "187127:100:raw8:74eb828d95fc", "187115:300:rle:e808ad60c739", "186770:300:rle:2ae6057ba0f1"),
                },
            },
            new TileCase
            {
                Client = "wow", X = 34, Y = 45, Wdt = AzerothWdt, Root = 778412, Tex0 = 778415, Mphd = 0x3CA, RootSha12 = "662469a8fc59", TexSha12 = "9071d563ba4b",
                Counts = Counts(effectStatic: 4, textured: 252), VertexColorChunks = 41, Raw8 = 24, Rle = 572, MdidEntries = 10,
                TextureIds = new uint[] { 186934, 186951, 186941, 186936, 188903, 186947, 186949, 188893, 252273, 188881 }, PlainOnly = new uint[0],
                Refs = 852, Zero = 0, Dup = 842, TileState = TerrainTileMaterialState.Partial,
                Pins = new[]
                {
                    Pin(3, TerrainMaterialState.LayerEffectStatic, false, 0, "186934:0:base:-", "186951:300:rle:8cd3b1ca7d38", "186947:3ee:rle:3ea74366c285", "188903:300:rle:cb623dbfc30b"),
                },
            },
            new TileCase
            {
                Client = "wow", X = 30, Y = 49, Wdt = AzerothWdt, Root = 777632, Tex0 = 777635, Mphd = 0x3CA, RootSha12 = "d8d709484920", TexSha12 = "117ae1ae3d43",
                Counts = Counts(textured: 256), VertexColorChunks = 36, Raw8 = 2, Rle = 621, MdidEntries = 11,
                TextureIds = new uint[] { 187094, 187127, 187115, 186770, 187125, 187138, 189006, 189008, 971416, 878975, 189004 }, PlainOnly = new uint[0],
                Refs = 879, Zero = 0, Dup = 868, TileState = TerrainTileMaterialState.Textured,
                HeightIds = new uint[] { 971415, 878974 }, HeightRefs = 12, HeightDup = 10,
                Pins = new[]
                {
                    Heights(Pin(52, Tx, true, 0, "187127:0:base:-", "186770:100:raw8:4043a9d22dfd", "971416:300:rle:c685beb2f56e", "187115:300:rle:be3961467080"), 0, 0, 971415, 0),
                    Heights(Pin(68, Tx, true, 0, "187127:0:base:-", "971416:300:rle:b5e3443d1994", "878975:300:rle:126ce3d3049a", "186770:300:rle:6425d5ec11ea"), 0, 971415, 878974, 0),
                },
            },
            new TileCase
            {
                Client = "wow_classic_beta", X = 32, Y = 48, Wdt = AzerothWdt, Root = 778027, Tex0 = 778030, Mphd = 0x3CA, RootSha12 = "d009dbc64a8f", TexSha12 = "7b24506dd4de",
                Counts = Counts(textured: 256), VertexColorChunks = 256, Raw8 = 1, Rle = 636, MdidEntries = 7,
                TextureIds = new uint[] { 187138, 187115, 187127, 187125, 186770, 187100, 187112 }, PlainOnly = new uint[0],
                Refs = 893, Zero = 0, Dup = 886, TileState = TerrainTileMaterialState.Textured,
                Pins = new[] { Pin(162, Tx, true, 0, "187127:0:base:-", "186770:300:rle:56c3b5e5fc30", "187125:300:rle:123bc76d6763", "187115:300:rle:8a7d3ea2a3f6") },
            },
            new TileCase
            {
                Client = "wow_classic_beta", X = 31, Y = 47, Wdt = AzerothWdt, Root = 777822, Tex0 = 777825, Mphd = 0x3CA, RootSha12 = "8226403c2b7e", TexSha12 = "91aad24744ec",
                Counts = Counts(textured: 256), VertexColorChunks = 256, Raw8 = 18, Rle = 450, MdidEntries = 11,
                TextureIds = new uint[] { 187127, 187138, 187115, 186770, 187125, 189000, 322141, 327252, 357282, 357276, 189020 }, PlainOnly = new uint[0],
                Refs = 724, Zero = 0, Dup = 713, TileState = TerrainTileMaterialState.Textured,
                Pins = new[]
                {
                    Pin(48, Tx, true, 0, "187127:0:base:-", "186770:300:rle:9af2b6ce29cd", "187125:300:rle:a6c978e12167", "187115:300:rle:ceeb0c12f204", "187138:300:rle:9a1bdbb4e524"),
                },
            },
            new TileCase
            {
                Client = "wow_classic_era", X = 32, Y = 50, Wdt = AzerothWdt, Root = 778037, Tex0 = 778040, Mphd = 0x248, RootSha12 = "cd27f188c894", TexSha12 = "1f5f92c4ac04",
                Counts = Counts(alpha: 256), Shadow = 256, Raw4 = 693, MdidEntries = 10,
                TextureIds = new uint[0], PlainOnly = new uint[0], TileState = TerrainTileMaterialState.Unsupported,
                Pins = new[]
                {
                    Pin(51, TerrainMaterialState.AlphaUnsupported, false, 0, "187127:0:base:-", "186770:100:raw4:14b3f3d32f24", "187125:100:raw4:bf8786e54409", "187115:100:raw4:6f03617f19f5"),
                    Pin(0, TerrainMaterialState.AlphaUnsupported, false, 0, "187127:0:base:-", "187138:100:raw4:baf79f89deaf", "187115:100:raw4:eebeebbc816b", "187125:100:raw4:ee5a1d6f7874"),
                    Pin(150, TerrainMaterialState.AlphaUnsupported, false, 0, "187127:0:base:-", "187138:100:raw4:b36a90c89d19", "187115:100:raw4:8b8cb470e9e5", "187125:100:raw4:759e998131e8"),
                    Pin(47, TerrainMaterialState.AlphaUnsupported, false, 0, "187127:0:base:-", "187115:100:raw4:c635938e56d8", "186770:100:raw4:590a2b27f7f9", "187125:100:raw4:60bca697a960"),
                    Pin(67, TerrainMaterialState.AlphaUnsupported, false, 0, "187127:0:base:-", "186770:100:raw4:e493ef8757fb", "187125:100:raw4:5ed331f68d57"),
                    Pin(253, TerrainMaterialState.AlphaUnsupported, false, 0, "187082:0:base:-", "187094:100:raw4:fdd603504554", "187088:100:raw4:807f28999631"),
                },
            },
            new TileCase
            {
                Client = "wow_classic_era", X = 32, Y = 48, Wdt = AzerothWdt, Root = 778027, Tex0 = 778030, Mphd = 0x248, RootSha12 = "1f4fd2f2e864", TexSha12 = "5698079cba10",
                Counts = Counts(alpha: 256), Shadow = 210, Raw4 = 637, MdidEntries = 7,
                TextureIds = new uint[0], PlainOnly = new uint[0], TileState = TerrainTileMaterialState.Unsupported,
                Pins = new[]
                {
                    Pin(253, TerrainMaterialState.AlphaUnsupported, false, 0, "187127:0:base:-", "186770:100:raw4:bf5f12044795", "187115:100:raw4:c9492d1ab96c", "187125:100:raw4:f0d8ed804e1e"),
                    Pin(97, TerrainMaterialState.AlphaUnsupported, false, 0, "187138:0:base:-", "187127:100:raw4:cc8b789bc060", "187115:100:raw4:f2dc48773ccb", "186770:100:raw4:84f713499937"),
                },
            },
            new TileCase
            {
                Client = "wow_classic", X = 31, Y = 48, Wdt = AzerothWdt, Root = 777827, Tex0 = 777830, Mphd = 0x24E, RootSha12 = "2c93198851c5", TexSha12 = "2ca736edc3a3",
                Counts = Counts(unresolved: 2, textured: 254), Shadow = 171, VertexColorChunks = 71, Raw8 = 2, Rle = 570, MdidEntries = 15,
                TextureIds = new uint[] { 320468, 189020, 320470, 451141, 304643, 187115, 187127, 187125, 187138, 451145, 187082, 451147, 186770, 187100 },
                PlainOnly = new uint[] { 303951 }, Refs = 820, Zero = 0, Dup = 806, TileState = TerrainTileMaterialState.Partial,
                Pins = new[]
                {
                    Pin(66, TerrainMaterialState.LayerEffectUnresolved, true, 0, "303951:400:base:-", "451145:300:rle:0216596734d6", "451147:100:raw8:c03a50f882f6", "187082:300:rle:35a6a07a5725"),
                    Pin(149, Tx, true, 0, "187115:0:base:-", "187138:300:rle:d27f1b47c8d1", "186770:300:rle:36f95f2da077", "187127:300:rle:1ead07110b06"),
                },
            },
            // Era map 2853 tile 34_50: an 8-bit Era map (the design's Era golden tile).
            new TileCase
            {
                Client = "wow_classic_era", Label = "map 2853", X = 34, Y = 50, Wdt = 6357503, Root = 6358761, Tex0 = 6358764, Mphd = 0x3CA,
                RootDir = "materials/wow_classic_era/stage04_core", TexDir = "materials/wow_classic_era/era_maptex_probe",
                RootSha = "5cbb6ed02af5852f4f7aa229d790392dbd13d6bbf9c178800ee0d0831796971c", TexSha = "01af4b77e2968dd8fd908fd24d524206c10a28a3fbae854044ed97ceaaeea1fe",
                Counts = Counts(textured: 256), Raw8 = 1, Rle = 681, MdidEntries = 10,
                TextureIds = new uint[] { 187127, 187115, 187125, 187100, 187138, 186770, 187074, 187088, 187082, 187094 }, PlainOnly = new uint[0],
                Refs = 938, Zero = 0, Dup = 928, TileState = TerrainTileMaterialState.Textured,
                Pins = new[]
                {
                    Pin(0, Tx, false, 0, "187127:0:base:-", "187115:300:rle:9b1e574ca974", "187125:300:rle:07a8e7083d8e", "187100:300:rle:a59327fe71e6"),
                    Pin(100, Tx, false, 0, "187127:0:base:-", "186770:300:rle:3ad856c02536", "187125:300:rle:9c11661c91c9", "187115:300:rle:66a91e8f3d02"),
                },
            },
            // Retail map 169 tile 24_27 (MPHD 0x2C8, 8-bit): MDID [0, 249604, 0, 0] -- real drawable layers with texture id 0.
            new TileCase
            {
                Client = "wow", Label = "map 169", X = 24, Y = 27, Wdt = 780817, Root = 780828, Tex0 = 780831, Mphd = 0x2C8,
                RootDir = "materials/wow/stage04_core", TexDir = "materials/wow/stage04_core",
                RootSha = "adef578214124f7a8c14f22d64849b83eed8579f3c393018280eb7d048428b2e", TexSha = "af7017e9e6424f42927a043a49e193ad2f5945867d013ee191c19e8b5e990a04",
                Counts = Counts(missing: 256), Raw8 = 1, Rle = 49, MdidEntries = 4,
                TextureIds = new uint[] { 249604 }, PlainOnly = new uint[0], Refs = 306, Zero = 295, Dup = 10, TileState = TerrainTileMaterialState.Partial,
            },
            // Retail map 4914790 tile 41_27 (MPHD 0x3CA): an ocean tile, no layer anywhere but MCCV with flag 0x40 on every
            // chunk -- neutral chunks get no vertex colours (v3 delta, section 5).
            new TileCase
            {
                Client = "wow", Label = "map 4914790", X = 41, Y = 27, Wdt = 4914790, Root = 4920553, Tex0 = 4920556, Mphd = 0x3CA,
                RootDir = "materials/wow/modern_probe", TexDir = "materials/wow/modern_probe",
                RootSha = "6a9975a52f42aee5a059c7bf03a7166727bddb81f045322bc9754ce8e8b5913c", TexSha = "e4cb187bbc1d8159eeb1aae60a9fc6bda8b5e55bd888f2953f470683136d38c7",
                Counts = Counts(none: 256), VertexColorChunks = 0, MdidEntries = 0,
                TextureIds = new uint[0], PlainOnly = new uint[0], TileState = TerrainTileMaterialState.Unsupported,
                Pins = new[] { Pin(0, TerrainMaterialState.NoLayers, false, 0) },
            },
            // Retail map 1923857 tile 27_19 (MPHD 0x34E: 8-bit, 0x80 clear) whose MHID names a height texture for every
            // MDID entry and whose MTXP has height values: both are ignored (H = 1), the chunks are Textured and no height
            // texture is listed (v3 delta, section 1). Pinned in materials/wow/stage04_golden (manifest, build checked).
            new TileCase
            {
                Client = "wow", Label = "map 1923857", X = 27, Y = 19, Wdt = 1923857, Root = 1923654, Tex0 = 1923657, Mphd = 0x34E,
                RootDir = "materials/wow/stage04_golden", TexDir = "materials/wow/stage04_golden",
                RootSha = "c4a19e9b4cb32d6e7dee6034f5e33cfcd9d0938ae931172d905695d6612b41c7", TexSha = "bce9551c3e4c4118354b50d599cca317686f5242a40cac387b6e52e78a63f122",
                Counts = Counts(textured: 256), VertexColorChunks = 223, Raw8 = 143, Rle = 399, MdidEntries = 10, Notes = 1,
                TextureIds = new uint[] { 880021, 794740, 803790, 852287, 841269, 841800, 794728, 794746, 794738, 794732 }, PlainOnly = new uint[0],
                Refs = 798, Zero = 0, Dup = 788, TileState = TerrainTileMaterialState.Textured,
            },
            // Retail map 2923 tile 32_24 (MPHD 0x3CA; WDT 6838755, map texture 7473747): MTXP scale nibble 4 on 4 of its 16
            // MDID entries and 115 chunks of 6..8 layers (83 / 27 / 5); every layer names a height texture. All 256 chunks
            // are drawable under the round-3 rules (nibble 4 = 0.5 repeats with the chunk's UV phase, up to 8 layers);
            // 371 slices = 256 + one second slice per chunk of 6..8 layers. Extracted read-only into
            // materials/wow/stage04_nibble4 (manifest, build 12.1.0.69933).
            new TileCase
            {
                Client = "wow", Label = "map 2923", X = 32, Y = 24, Wdt = 6838755, Root = 7473884, Tex0 = 7473887, Mphd = 0x3CA,
                RootDir = Nibble4Dir, TexDir = Nibble4Dir,
                RootSha = "c00e921d15a37cb6dd2b48169979e461f7791bb21ccc59e7f39b6c1eb56f76d2", TexSha = "8a9a881a30e80541657c0d1f58531da4b3e809b4d46b72266448bc36d315028a",
                Counts = Counts(textured: 256), VertexColorChunks = 256, Raw8 = 28, Rle = 1019, MdidEntries = 16,
                TextureIds = new uint[] { 6717884, 6049365, 5928543, 6156542, 6021896, 5900005, 5876854, 5900000, 5900003, 6225354, 6339465, 6163585, 5906217,
                                          6717887, 6021899, 6224613 },
                PlainOnly = new uint[0], Refs = 1303, Zero = 0, Dup = 1287, TileState = TerrainTileMaterialState.Textured,
                HeightIds = new uint[] { 6717883, 6049364, 5928542, 6156541, 6021895, 5900004, 5876853, 5899999, 5900002, 6225353, 6339464, 6163584, 5906216,
                                         6717886, 6021898, 6224612 },
                HeightRefs = 1303, HeightDup = 1287, Slices = 371, PlainSliceCount = 0,
                Pins = new[]
                {
                    // 7 layers, ix 2 / iy 0 (phase 0 everywhere), nibble 4 on the base layer and layers 2 and 6 (6: second slice)
                    Rep(Heights(Pin(2, Tx, true, 0, "5900000:0:base:-", "6049365:300:rle:fd8bf8ddc4dd", "5900005:300:rle:3536c794a8b4", "6156542:300:rle:ba101777fb0a",
                                    "5928543:300:rle:292a7bbe166e", "5876854:300:rle:eaa3da90d7a9", "5900003:300:rle:464ed33567da"),
                                5899999, 6049364, 5900004, 6156541, 5928542, 5876853, 5900002), 0.5f, 1, 0.5f, 4, 2, 1, 0.5f),
                    // 2 layers, ix 7 / iy 0: phase (0.5, 0) on the nibble-4 layer
                    Rep(Heights(Pin(7, Tx, true, 0, "5876854:0:base:-", "5900005:300:rle:5c846f4492c0"), 5876853, 5900004), 1, 0.5f),
                    // 4 layers, ix 6 / iy 1: phase (0, 0.5)
                    Rep(Heights(Pin(22, Tx, true, 0, "5928543:0:base:-", "5876854:300:rle:1968990a97a2", "5900005:300:rle:661eb6f25565", "6156542:300:rle:265a52957aeb"),
                                5928542, 5876853, 5900004, 6156541), 2, 1, 0.5f, 4),
                    // 3 layers, ix 7 / iy 1: phase (0.5, 0.5)
                    Rep(Heights(Pin(23, Tx, true, 0, "5928543:0:base:-", "5876854:300:rle:f7f3afab7186", "5900005:300:rle:3d55fdb8c023"), 5928542, 5876853, 5900004),
                        2, 1, 0.5f),
                    // 8 layers, ix 1 / iy 9: phase (0.5, 0.5) on layers 5 and 7, both in the second slice
                    Rep(Heights(Pin(145, Tx, true, 0, "5928543:0:base:-", "5876854:300:rle:1b1a6ed1c666", "6225354:300:rle:0fc7a425f5d5", "6156542:300:rle:ec3a7de032ff",
                                    "6021899:300:rle:a2f8ebe2991b", "5900005:300:rle:4d4c2896e8e6", "6021896:300:rle:b1201e329805", "5900000:300:rle:c76ffdab3e32"),
                                5928542, 5876853, 6225353, 6156541, 6021898, 5900004, 6021895, 5899999), 2, 1, 1, 4, 2, 0.5f, 2, 0.5f),
                    // 8 layers, ix 1 / iy 13
                    Rep(Heights(Pin(209, Tx, true, 0, "5928543:0:base:-", "6225354:300:rle:4d066a61160e", "6339465:300:rle:4a8e2263d5b6", "5900005:300:rle:8f27edafdc14",
                                    "6156542:300:rle:aaa2f8958d32", "5900003:300:rle:7a59ecb54a9d", "5876854:300:rle:2ed826b2bc86", "6021896:300:rle:c92fb1150d6c"),
                                5928542, 6225353, 6339464, 5900004, 6156541, 5900002, 5876853, 6021895), 2, 1, 2, 0.5f, 4, 0.5f, 1, 2),
                },
            },
        };

        const string Nibble4Dir = "materials/wow/stage04_nibble4";

        static ChunkPin Heights(ChunkPin p, params uint[] heightIds)
        {
            p.HeightIds = heightIds;
            return p;
        }

        static ChunkPin Rep(ChunkPin p, params float[] repeats)
        {
            p.Repeats = repeats;
            return p;
        }

        static int fails, passes;
        static Action<string> say;

        static void Verdict(bool ok, string name, string detail = null)
        {
            if (ok) passes++; else fails++;
            say((ok ? "  PASS  " : "  FAIL  ") + name + (detail != null ? " (" + detail + ")" : ""));
        }

        public static int Run(string corpus, Action<string> output)
        {
            say = output;
            fails = 0;
            passes = 0;
            string pinsPath = FindPins();
            if (pinsPath == null)
            {
                Verdict(false, "pins: Tools/world-audit/tests/corpus_hashes.json not found");
                return Finish();
            }
            Pins pins = LoadPins(pinsPath);
            var buildMedians = new List<string>();
            foreach (TileCase k in Cases)
                RunCase(corpus, pins, k, buildMedians);
            say("    build median ms per tile: " + string.Join(", ", buildMedians));
            HeightTile(corpus, pins);
            MapTextureFits(corpus, pins);
            BlpHeaders(corpus);
            return Finish();
        }

        static int Finish()
        {
            say(string.Format("TerrainMaterialCorpusTests: {0} passed, {1} failed", passes, fails));
            return fails;
        }

        static string Tag(TileCase k) { return k.Client + (k.Label != null ? " " + k.Label : "") + " " + k.X + "_" + k.Y; }

        static void RunCase(string corpus, Pins pins, TileCase k, List<string> buildMedians)
        {
            string tag = Tag(k);
            string wdtRel = "wdt/" + k.Client + "/" + k.Wdt + ".bin";
            string problem = CheckPinned(corpus, pins, wdtRel);
            if (problem != null)
            {
                Verdict(false, tag + ": WDT " + k.Wdt + " has its pinned bytes", problem);
                return;
            }
            WdtFile wdt = WdtParser.Parse(File.ReadAllBytes(Path.Combine(corpus, wdtRel)), tag + " wdt");
            WdtTileFiles files = default(WdtTileFiles);
            bool named = wdt.TileExists(k.X, k.Y) && wdt.TryGetTileFiles(k.X, k.Y, out files);
            uint root = named ? files[WdtTileFileRole.RootAdt] : 0, tex0 = named ? files[WdtTileFileRole.Tex0Adt] : 0;
            Verdict(named && root == k.Root && tex0 == k.Tex0 && wdt.Header.Flags == k.Mphd,
                    tag + ": the WDT's MAID names root " + k.Root + " and tex0 " + k.Tex0 + "; MPHD 0x" + k.Mphd.ToString("X"),
                    string.Format("MAID root {0}, tex0 {1}, MPHD 0x{2:X}", root, tex0, wdt.Header.Flags));
            byte[] rootBytes = ReadInput(corpus, pins, k.RootDir ?? string.Format(Az, k.Client), k.Root, k.RootSha, k.RootSha12, tag + ": root");
            byte[] texBytes = ReadInput(corpus, pins, k.TexDir ?? string.Format(Az, k.Client), k.Tex0, k.TexSha, k.TexSha12, tag + ": tex0");
            if (rootBytes == null || texBytes == null)
                return;

            var ms = new List<double>();
            AdtRoot parsedRoot = null;
            AdtTex parsedTex = null;
            TerrainTileMaterial m = null;
            for (int run = 0; run < 5; run++)
            {
                parsedRoot = AdtParser.ParseRoot(rootBytes, k.X, k.Y, tag + " root");
                parsedTex = AdtParser.ParseTex(texBytes, tag + " tex0");
                var sw = Stopwatch.StartNew();
                m = TerrainMaterialBuilder.Build(parsedRoot, parsedTex, wdt.Header.Flags);
                ms.Add(sw.Elapsed.TotalMilliseconds);
            }
            say(string.Format(CultureInfo.InvariantCulture, "    timing {0}: build ms {1}", tag, string.Join(" ", ms.Select(v => v.ToString("0.00", CultureInfo.InvariantCulture)))));
            buildMedians.Add(tag + " " + Median(ms).ToString("0.00", CultureInfo.InvariantCulture));

            int[] got = Enumerable.Range(0, TerrainTileMaterial.StateCount).Select(s => m.Count((TerrainMaterialState)s)).ToArray();
            Verdict(got.SequenceEqual(k.Counts) && m.ShadowMapsIgnored == k.Shadow && m.VertexColorMismatches == k.Mismatch,
                    tag + ": chunk states and notes as computed independently",
                    string.Join(", ", m.CounterPairs().Where(p => p.Value > 0).Select(p => p.Key + " " + p.Value)));
            Verdict(m.Raw8Layers == k.Raw8 && m.RleLayers == k.Rle && m.Raw4Layers == k.Raw4 && m.VertexColorChunks == k.VertexColorChunks &&
                    parsedTex.DiffuseTextureIds.Length == k.MdidEntries && m.AlphaBitDepth == ((k.Mphd & 0x84) != 0 ? 8 : 4),
                    tag + ": alpha storage, vertex-colour chunks, MDID entries, MPHD alpha depth",
                    string.Format("raw8 {0}, rle {1}, raw4 {2}, MCCV applied {3}, MDID {4}, {5}", m.Raw8Layers, m.RleLayers, m.Raw4Layers, m.VertexColorChunks,
                                  parsedTex.DiffuseTextureIds.Length, m.AlphaFormat));
            Verdict(m.TextureIds.SequenceEqual(k.TextureIds) && m.PlainOnlyTextureIds.SequenceEqual(k.PlainOnly) && m.LayerReferences == k.Refs &&
                    m.ZeroIdReferences == k.Zero && m.DuplicateReferences == k.Dup,
                    tag + ": texture ids of the drawable chunks in first use, plain-only ids, references",
                    m.TextureIds.Length + " ids [" + string.Join(" ", m.TextureIds) + "], plain [" + string.Join(" ", m.PlainOnlyTextureIds) + "], references " +
                    m.LayerReferences + " (id 0: " + m.ZeroIdReferences + ", repeats: " + m.DuplicateReferences + ")");
            Verdict(m.UseHeightTextures == ((k.Mphd & 0x80) != 0) && m.HeightTextureIds.SequenceEqual(k.HeightIds) && m.HeightLayerReferences == k.HeightRefs &&
                    m.HeightDuplicateReferences == k.HeightDup && m.Notes.Count == k.Notes,
                    tag + ": height texture ids (MHID) of the drawable chunks in first use" + ((k.Mphd & 0x80) != 0 ? "" : " (none: MPHD 0x80 clear)") + ", notes",
                    "heights [" + string.Join(" ", m.HeightTextureIds) + "], references " + m.HeightLayerReferences + " (repeats " + m.HeightDuplicateReferences +
                    "); notes " + m.Notes.Count + (m.Notes.Count > 0 ? ": " + string.Join(" | ", m.Notes) : ""));
            int plain = got[(int)TerrainMaterialState.LayerEffectUnresolved];
            int wantSlices = k.Slices >= 0 ? k.Slices : m.DrawableChunks, wantPlain = k.PlainSliceCount >= 0 ? k.PlainSliceCount : plain;
            bool layout = m.Chunks.Where(c => c.Drawable || c.PlainDiagnostic).All(c => c.AlphaSliceCount == (c.Layers.Length > 5 ? 2 : 1) &&
                                                                                        c.AlphaSlice2 == (c.Layers.Length > 5 ? c.AlphaSlice + 1 : -1));
            Verdict(m.State == k.TileState && m.DrawableSlices == wantSlices && m.PlainSlices == wantPlain && layout &&
                    m.BuildAlphaArray(false).Length == m.DrawableSlices * TerrainChunkMaterial.AlphaSliceBytes,
                    tag + ": tile state " + TerrainTileMaterial.TileStateName(k.TileState) + ", " + wantSlices + " + " + wantPlain +
                    " slices (one per drawable or plain-diagnostic chunk, a second right after it for 6..8 layers)",
                    TerrainTileMaterial.TileStateName(m.State) + "; slices " + m.DrawableSlices + " + " + m.PlainSlices + " plain; alpha array " +
                    m.DrawableSlices * TerrainChunkMaterial.AlphaSliceBytes / 1024 + " KiB" + (m.Reason != null ? "; " + m.Reason : ""));

            foreach (ChunkPin p in k.Pins)
                CheckPin(tag, m, parsedRoot, parsedTex, p);
        }

        static void CheckPin(string tag, TerrainTileMaterial m, AdtRoot root, AdtTex tex, ChunkPin p)
        {
            TerrainChunkMaterial c = m.Chunks[p.Index];
            AdtTexChunk tc = tex.Mcnk[p.Index];
            var problems = new List<string>();
            if (c.State != p.State) problems.Add("state " + TerrainTileMaterial.StateName(c.State) + ": " + c.Reason);
            if (!c.Layers.Select(l => l.TextureFileDataID).SequenceEqual(p.Ids)) problems.Add("ids " + string.Join(" ", c.Layers.Select(l => l.TextureFileDataID)));
            if (!c.Layers.Select(l => l.Flags).SequenceEqual(p.Flags)) problems.Add("flags " + string.Join(" ", c.Layers.Select(l => "0x" + l.Flags.ToString("X"))));
            var hashes = new List<string>();
            for (int k = 0; k < c.Layers.Length && k < p.Ids.Length; k++)
            {
                string storage = k == 0 ? "base" : c.Layers[k].Storage == AlphaStorage.Compressed ? "rle" : c.Layers[k].Storage == AlphaStorage.Uncompressed8 ? "raw8" : "raw4";
                if (storage != p.Storage[k]) problems.Add("layer " + k + " storage " + storage);
                if (k == 0)
                    continue;
                string parser = Sha12(tc.Alpha[k].Samples);
                string fromSlice = null;
                if (c.Alpha != null && k < TerrainMaterialBuilder.MaxDrawnLayers)
                {
                    int sl, chan;
                    TerrainChunkMaterial.AlphaChannelOf(k, out sl, out chan);
                    var ch = new byte[4096];
                    if (sl < c.AlphaSliceCount)
                        for (int i = 0; i < 4096; i++) ch[i] = c.Alpha[sl * TerrainChunkMaterial.AlphaSliceBytes + 4 * i + chan];
                    fromSlice = Sha12(ch);
                }
                hashes.Add((fromSlice ?? parser) + (fromSlice != null ? " (slice)" : " (parser)"));
                if (parser != p.Hash12[k] || (fromSlice != null && fromSlice != p.Hash12[k]))
                    problems.Add("layer " + k + " alpha " + parser + (fromSlice != null ? " / slice " + fromSlice : ""));
            }
            bool drawnWithSlice = c.Drawable || c.PlainDiagnostic;
            if ((c.Alpha != null) != drawnWithSlice) problems.Add("slice " + (c.Alpha != null ? "present" : "absent"));
            if (c.HoleMask != p.Holes) problems.Add("holes 0x" + c.HoleMask.ToString("X"));
            if ((c.VertexColors != null) != p.VertexColors) problems.Add("vertex colours " + (c.VertexColors != null ? "applied" : "not applied"));
            if (p.HeightIds != null && !c.Layers.Select(l => l.HeightTextureFileDataID).SequenceEqual(p.HeightIds))
                problems.Add("height ids " + string.Join(" ", c.Layers.Select(l => l.HeightTextureFileDataID)));
            if (p.Repeats != null)
            {
                // the UV phase by the research's statement of the rule: (0.5 * (ix % 2), 0.5 * (iy % 2)) at 0.5 repeats, else 0
                int ix = p.Index % 16, iy = p.Index / 16;
                if (!c.Layers.Select(l => l.RepeatsPerChunk).SequenceEqual(p.Repeats))
                    problems.Add("repeats " + string.Join(" ", c.Layers.Select(l => l.RepeatsPerChunk.ToString(CultureInfo.InvariantCulture))));
                if (!c.Layers.All(l => l.PhaseU == (l.RepeatsPerChunk == 0.5f && ix % 2 == 1 ? 0.5f : 0f) &&
                                       l.PhaseV == (l.RepeatsPerChunk == 0.5f && iy % 2 == 1 ? 0.5f : 0f)))
                    problems.Add("phases " + string.Join(" ", c.Layers.Select(l => "(" + l.PhaseU.ToString(CultureInfo.InvariantCulture) + ", " +
                                                                                     l.PhaseV.ToString(CultureInfo.InvariantCulture) + ")")));
            }
            if (c.VertexColors != null)
            {
                byte[] raw = root.Mcnk[p.Index].VertexColorsRaw;
                for (int v = 0; v < 145; v++)
                    if (c.VertexColors[3 * v] != raw[4 * v + 2] / 127f || c.VertexColors[3 * v + 1] != raw[4 * v + 1] / 127f || c.VertexColors[3 * v + 2] != raw[4 * v] / 127f)
                    {
                        problems.Add("vertex colour " + v);
                        break;
                    }
            }
            Verdict(problems.Count == 0, string.Format("{0} MCNK {1}: {2}, {3} layers, census alpha SHA-256 {4}{5}{6}", tag, p.Index, TerrainTileMaterial.StateName(p.State),
                                                       p.Ids.Length, string.Join(" ", p.Hash12.Skip(1)),
                                                       p.HeightIds != null ? ", height ids " + string.Join(" ", p.HeightIds) : "",
                                                       p.Repeats != null ? ", repeats " + string.Join(" ", p.Repeats.Select(r => r.ToString(CultureInfo.InvariantCulture))) +
                                                                           " with the UV phase of ix " + p.Index % 16 + ", iy " + p.Index / 16 : ""),
                    problems.Count > 0 ? string.Join("; ", problems) : string.Join(", ", hashes) + (c.Reason != null ? "; " + c.Reason : ""));
        }

        // ================================================================================== height textures (30_49)

        /// <summary>Retail 30_49: the height textures its drawable chunks read are local (a verified copy of each,
        /// BLP2 with an alpha channel: the height is the alpha), its height-textured chunks are Textured with the
        /// MTXP height values the tex0 holds, and a failed height texture makes exactly the chunks reading it
        /// HeightTextureMissing (counts computed independently: 971415 is read by 10 chunks, 878974 by 2).</summary>
        static void HeightTile(string corpus, Pins pins)
        {
            const string tag = "wow 30_49";
            TerrainTileMaterial m = BuildCorpusTile(corpus, pins, "wow", AzerothWdt, 30, 49, null, null, null, null, false, tag);
            if (m == null)
                return;
            var local = new List<string>();
            bool allLocal = true;
            foreach (uint id in m.HeightTextureIds)
            {
                string problem;
                byte[] b = ReadMaterialFile(corpus, pins, "wow", id, out problem);
                bool ok = b != null;
                if (ok)
                {
                    BlpHeader h = BlpDecoder.ReadHeader(b);
                    BlpImage img = BlpDecoder.Decode(b, 0);
                    ok = h.AlphaDepth == 8 && img.HasAlpha;
                    problem = h.Encoding + " " + h.Width + "x" + h.Height + ", alpha " + h.AlphaDepth + " bits";
                }
                allLocal &= ok;
                local.Add(id + ": " + problem);
            }
            Verdict(allLocal && m.HeightTextureIds.Length == 2, tag + ": every height texture the drawable chunks read is local (verified copy, BLP2 with 8-bit alpha)",
                    string.Join("; ", local));
            TerrainChunkMaterial c52 = m.Chunks[52], c68 = m.Chunks[68];
            Verdict(c52.State == TerrainMaterialState.Textured && c68.State == TerrainMaterialState.Textured && c52.NeedsHeightTextures &&
                    c52.Layers[2].HeightScale == 5f && c52.Layers[2].HeightOffset == 1f && c52.Layers[2].RepeatsPerChunk == 2 &&
                    c68.Layers[2].HeightScale == 4f && c68.Layers[2].HeightOffset == 0.5f && c68.Layers[2].RepeatsPerChunk == 8 &&
                    c52.Layers[0].HeightScale == 0f && c52.Layers[0].HeightOffset == 1f && m.Chunks.Count(c => c.NeedsHeightTextures) == 10,
                    tag + ": MCNK 52 and 68 are Textured; MTXP (scale, offset) 971416 (5.0, 1.0) at 2 repeats, 878975 (4.0, 0.5) at 8; 10 chunks read height textures",
                    TerrainTileMaterial.StateName(c52.State) + " / " + TerrainTileMaterial.StateName(c68.State));
            m.ApplyHeightTextureFailures(new uint[] { 971415 });
            int a = m.Count(TerrainMaterialState.HeightTextureMissing);
            bool c68a = m.Chunks[68].State == TerrainMaterialState.HeightTextureMissing;
            m.ApplyHeightTextureFailures(new uint[] { 878974 });
            int b2 = m.Count(TerrainMaterialState.HeightTextureMissing);
            m.ApplyHeightTextureFailures(null);
            Verdict(a == 10 && c68a && b2 == 2 && m.Count(TerrainMaterialState.Textured) == 256 && m.State == TerrainTileMaterialState.Textured,
                    tag + ": a failed height texture: 971415 -> 10 chunks heightTextureMissing, 878974 -> 2; none -> all textured again",
                    a + " / " + b2);
        }

        // ================================================================================== map-texture fit

        /// <summary>One kept-block set of a fit case and the measurements the design records for it (4x4-block RMSE,
        /// the Python reference's composer; NaN = not recorded).</summary>
        sealed class FitSet
        {
            public string Name;
            public Func<TerrainChunkMaterial, bool> Keep;
            public int Blocks;
            public double Verified, Toggled = double.NaN, Plain;
            /// <summary>Heights sampled at 8 repeats instead of the layer's MTXP repeats (the review's
            /// measurement; NaN = this alternative is not run for the set).</summary>
            public double Heights8 = double.NaN;
            /// <summary>The MTXP scale nibble 4 alternatives (the Python reference's measurements; NaN = not run): the
            /// nibble-4 layers (diffuse and height texture) with chunk-local UV (phase 0), as 1 repeat, as 8 repeats.</summary>
            public double ChunkLocal = double.NaN, OneRepeat = double.NaN, EightRepeats = double.NaN;
        }

        sealed class FitCase
        {
            public string Label, Client = "wow";
            public uint Wdt = AzerothWdt, MapTexture;
            public int X, Y;
            public string RootDir, TexDir, MapDir;          // null = the pinned corpus
            public string RootSha, TexSha, MapSha;          // full SHA-256 for files outside the pinned corpus
            public FitSet[] Sets;
        }

        static readonly FitCase[] Fits =
        {
            // the height-textured tile: full formula on its height-textured chunks (delta section 1)
            new FitCase
            {
                Label = "wow 30_49", X = 30, Y = 49, MapTexture = 1293863,
                Sets = new[]
                {
                    new FitSet { Name = "height-textured chunks", Keep = c => c.NeedsHeightTextures, Blocks = 640, Verified = 1.203, Toggled = 5.396, Plain = 7.405,
                                 Heights8 = 1.324 },
                    new FitSet { Name = "other textured chunks", Keep = c => c.State == TerrainMaterialState.Textured && !c.NeedsHeightTextures, Blocks = 15740,
                                 Verified = 1.156, Toggled = 1.156, Plain = 2.586 },
                },
            },
            new FitCase
            {
                Label = "wow 32_50", X = 32, Y = 50, MapTexture = 1293881,
                Sets = new[] { new FitSet { Name = "textured chunks", Keep = c => c.State == TerrainMaterialState.Textured, Blocks = 16384, Verified = 1.110, Plain = 1.744 } },
            },
            // lava: MCLY 0x40/0x80 chunks drawn with the static composite
            new FitCase
            {
                Label = "wow 34_46", X = 34, Y = 46, MapTexture = 1293753,
                Sets = new[]
                {
                    new FitSet { Name = "textured chunks", Keep = c => c.State == TerrainMaterialState.Textured, Blocks = 15424, Verified = 1.074, Plain = 2.437 },
                    new FitSet { Name = "layerEffectStatic (lava) chunks", Keep = c => c.State == TerrainMaterialState.LayerEffectStatic, Blocks = 704,
                                 Verified = 1.165, Plain = 4.597 },
                },
            },
            // an 0x80-clear 8-bit map whose MHID and MTXP name height textures and values: they must not enter
            new FitCase
            {
                Label = "wow map 1923857 27_19", Wdt = 1923857, X = 27, Y = 19, MapTexture = 1922956,
                RootDir = "materials/wow/stage04_golden", TexDir = "materials/wow/stage04_golden", MapDir = "materials/wow/stage04_golden",
                RootSha = "c4a19e9b4cb32d6e7dee6034f5e33cfcd9d0938ae931172d905695d6612b41c7", TexSha = "bce9551c3e4c4118354b50d599cca317686f5242a40cac387b6e52e78a63f122",
                MapSha = "a7afd1b915d8b4fea5583d5dd85bdc4b4539f57f5fd72e83d50a53d03a4a7b11",
                Sets = new[] { new FitSet { Name = "textured chunks", Keep = c => c.State == TerrainMaterialState.Textured, Blocks = 16217, Verified = 0.978, Toggled = 18.606, Plain = 3.027 } },
            },
            // MTXP scale nibble 4 (0.5 repeats, tile-continuous UV) and chunks of 6..8 layers. Recorded values: the Python
            // reference's composer as the fix-round-2 research ran it (design and the three nibble-4 alternatives: its
            // per-chunk values pooled over these sets) and a rerun against the same frozen reference for plain weights and
            // heights off (round-3 core, scratch py_alt.py); it reproduces the research's values exactly.
            new FitCase
            {
                Label = "wow map 2923 32_24", Wdt = 6838755, X = 32, Y = 24, MapTexture = 7473747,
                RootDir = Nibble4Dir, TexDir = Nibble4Dir, MapDir = Nibble4Dir,
                RootSha = "c00e921d15a37cb6dd2b48169979e461f7791bb21ccc59e7f39b6c1eb56f76d2", TexSha = "8a9a881a30e80541657c0d1f58531da4b3e809b4d46b72266448bc36d315028a",
                MapSha = "68a6334cecc7569967fc7bdcfac83bca636de4cf9f3abdae6f01a079b4dcd5eb",
                Sets = new[]
                {
                    new FitSet { Name = "drawable chunks", Keep = c => c.Drawable, Blocks = 16384, Verified = 1.057, Toggled = 3.913, Plain = 4.341,
                                 ChunkLocal = 3.477, OneRepeat = 3.302, EightRepeats = 3.033 },
                    new FitSet { Name = "chunks with a nibble-4 layer", Keep = c => c.Drawable && c.Layers.Any(l => l.ScaleExponent == 4), Blocks = 12032,
                                 Verified = 1.063, Toggled = 3.978, Plain = 4.489, ChunkLocal = 4.009, OneRepeat = 3.802, EightRepeats = 3.484 },
                    new FitSet { Name = "chunks of 6..8 layers", Keep = c => c.Drawable && c.Layers.Length >= 6, Blocks = 7360, Verified = 1.079, Toggled = 4.167,
                                 Plain = 4.622, ChunkLocal = 3.678, OneRepeat = 3.526, EightRepeats = 3.195 },
                },
            },
        };

        const int MapPixelsPerChunk = 32, MapSize = 16 * MapPixelsPerChunk;
        /// <summary>The height-sampling alternative: every height texture at 8 repeats per chunk (the default
        /// repeats) instead of its layer's MTXP repeats.</summary>
        const int HeightRepeatsAlternative = 8;
        /// <summary>The design's golden allowance: threshold = measured RMSE + 0.25 (v3 delta, section 4).</summary>
        const double GoldenAllowance = 0.25;

        /// <summary>The MTXP scale nibble 4 alternatives to the verified 0.5 repeats with the chunk's UV phase, applied to
        /// each nibble-4 layer (its diffuse and its height texture): the research's models b, c and d.</summary>
        static readonly KeyValuePair<string, Action<TerrainMaterialLayer>>[] Nibble4Alternatives =
        {
            new KeyValuePair<string, Action<TerrainMaterialLayer>>("nibble 4 with chunk-local UV (0.5 repeats, phase 0)", l => { l.PhaseU = 0f; l.PhaseV = 0f; }),
            new KeyValuePair<string, Action<TerrainMaterialLayer>>("nibble 4 as 1 repeat", l => { l.RepeatsPerChunk = 1f; l.PhaseU = 0f; l.PhaseV = 0f; }),
            new KeyValuePair<string, Action<TerrainMaterialLayer>>("nibble 4 as 8 repeats", l => { l.RepeatsPerChunk = 8f; l.PhaseU = 0f; l.PhaseV = 0f; }),
        };

        static void MapTextureFits(string corpus, Pins pins)
        {
            foreach (FitCase f in Fits)
            {
                var sw = Stopwatch.StartNew();
                TerrainTileMaterial built = BuildCorpusTile(corpus, pins, f.Client, f.Wdt, f.X, f.Y, f.RootDir, f.TexDir, f.RootSha, f.TexSha, false, f.Label);
                TerrainTileMaterial toggled = BuildCorpusTile(corpus, pins, f.Client, f.Wdt, f.X, f.Y, f.RootDir, f.TexDir, f.RootSha, f.TexSha,
                                                              true, null);
                if (built == null || toggled == null)
                    continue;
                byte[] mapBytes = ReadInput(corpus, pins, f.MapDir ?? string.Format(Az, f.Client), f.MapTexture, f.MapSha, null, f.Label + ": map texture (MAID word 5)");
                if (mapBytes == null)
                    continue;
                BlpImage map = BlpDecoder.Decode(mapBytes, 0);
                if (map.Width != MapSize || map.Height != MapSize)
                {
                    Verdict(false, f.Label + ": the map texture is " + MapSize + " px square", map.Width + "x" + map.Height);
                    continue;
                }
                var problems = new List<string>();
                float[] verified = TileComposite(corpus, pins, f.Client, built, TerrainBlendRule.HeightMix, problems);
                float[] other = TileComposite(corpus, pins, f.Client, toggled, TerrainBlendRule.HeightMix, problems);
                float[] plain = TileComposite(corpus, pins, f.Client, built, TerrainBlendRule.PlainWeights, problems);
                if (problems.Count > 0)
                {
                    Verdict(false, f.Label + ": every layer and height texture at a stored mip of one texel per map pixel, verified", string.Join("; ", problems.Distinct().Take(6)));
                    continue;
                }
                string heightsOther = built.UseHeightTextures ? "heights off (MPHD 0x" + toggled.MphdFlags.ToString("X") + ")"
                                                              : "heights on (MPHD 0x" + toggled.MphdFlags.ToString("X") + ")";
                // the height-sampling alternative: every height texture sampled at 8 repeats, the diffuse layers at their own
                var problems8 = new List<string>();
                float[] heights8 = f.Sets.Any(s => !double.IsNaN(s.Heights8))
                    ? TileComposite(corpus, pins, f.Client, built, TerrainBlendRule.HeightMix, problems8, HeightRepeatsAlternative) : null;
                // the MTXP scale nibble 4 alternatives: the same tile with its nibble-4 layers changed (diffuse and height
                // texture alike, as the layer's coordinate is theirs), composed by the compositor at those values
                var problemsN4 = new List<string>();
                var n4Alternatives = new List<KeyValuePair<string, float[]>>();
                if (f.Sets.Any(s => !double.IsNaN(s.ChunkLocal)))
                    foreach (var alt in Nibble4Alternatives)
                    {
                        TerrainTileMaterial changed = BuildCorpusTile(corpus, pins, f.Client, f.Wdt, f.X, f.Y, f.RootDir, f.TexDir, f.RootSha, f.TexSha, false, null);
                        if (changed == null)
                        {
                            problemsN4.Add(alt.Key + ": the tile did not build");
                            continue;
                        }
                        int layers = 0;
                        foreach (TerrainChunkMaterial c in changed.Chunks)
                            foreach (TerrainMaterialLayer l in c.Layers)
                                if (l.ScaleExponent == 4 && l.HasMtxp)
                                {
                                    alt.Value(l);
                                    layers++;
                                }
                        if (layers == 0)
                            problemsN4.Add(alt.Key + ": no nibble-4 layer");
                        n4Alternatives.Add(new KeyValuePair<string, float[]>(alt.Key + " (" + layers + " layers)",
                                                                             TileComposite(corpus, pins, f.Client, changed, TerrainBlendRule.HeightMix, problemsN4)));
                    }
                foreach (FitSet s in f.Sets)
                {
                    bool[] keep = KeepBlocks(built, s.Keep);
                    int blocks = keep.Count(k => k);
                    double rv = BlockRmse(verified, map.Rgba, keep), rt = BlockRmse(other, map.Rgba, keep), rp = BlockRmse(plain, map.Rgba, keep);
                    string nums = string.Format(CultureInfo.InvariantCulture,
                                                "{0} blocks; height mix {1:0.000} (recorded {2:0.000}), {3} {4:0.000}{5}, plain weights {6:0.000} (recorded {7:0.000})",
                                                blocks, rv, s.Verified, heightsOther, rt,
                                                double.IsNaN(s.Toggled) ? "" : string.Format(CultureInfo.InvariantCulture, " (recorded {0:0.000})", s.Toggled), rp, s.Plain);
                    say("    fit " + f.Label + ", " + s.Name + ": " + nums);
                    Verdict(blocks == s.Blocks && rv <= s.Verified + GoldenAllowance,
                            string.Format(CultureInfo.InvariantCulture, "{0}, {1}: the height mix fits the game's map texture within the recorded {2:0.000} + {3:0.00}",
                                          f.Label, s.Name, s.Verified, GoldenAllowance), nums);
                    Verdict(rp > rv + GoldenAllowance, f.Label + ", " + s.Name + ": plain weights fit worse (by more than the allowance)", nums);
                    if (!double.IsNaN(s.Toggled) && s.Toggled > s.Verified + GoldenAllowance)
                        Verdict(rt > rv + GoldenAllowance, f.Label + ", " + s.Name + ": the opposite MPHD 0x80 reading (" + heightsOther + ") fits worse", nums);
                    else
                        Verdict(Math.Abs(rt - rv) <= 0.01, f.Label + ", " + s.Name + ": " + heightsOther + " changes nothing here (no height texture or height value is read)", nums);
                    if (!double.IsNaN(s.ChunkLocal))
                    {
                        double[] recorded = { s.ChunkLocal, s.OneRepeat, s.EightRepeats };
                        for (int a = 0; a < n4Alternatives.Count && a < recorded.Length; a++)
                        {
                            double ra = problemsN4.Count == 0 ? BlockRmse(n4Alternatives[a].Value, map.Rgba, keep) : double.NaN;
                            string numsA = problemsN4.Count > 0 ? string.Join("; ", problemsN4.Distinct().Take(6))
                                : string.Format(CultureInfo.InvariantCulture, "height mix {0:0.000} (recorded {1:0.000}), {2} {3:0.000} (recorded {4:0.000})",
                                                rv, s.Verified, n4Alternatives[a].Key, ra, recorded[a]);
                            say("    fit " + f.Label + ", " + s.Name + ": " + numsA);
                            Verdict(problemsN4.Count == 0 && ra > rv + GoldenAllowance,
                                    f.Label + ", " + s.Name + ": " + n4Alternatives[a].Key.Substring(0, n4Alternatives[a].Key.LastIndexOf(" (", StringComparison.Ordinal)) +
                                    " fits worse (by more than the allowance)", numsA);
                        }
                        Verdict(n4Alternatives.Count == recorded.Length, f.Label + ", " + s.Name + ": the three nibble-4 alternatives ran", n4Alternatives.Count + " ran");
                    }
                    if (double.IsNaN(s.Heights8))
                        continue;
                    double r8 = problems8.Count == 0 ? BlockRmse(heights8, map.Rgba, keep) : double.NaN;
                    string nums8 = problems8.Count > 0 ? string.Join("; ", problems8.Distinct().Take(6))
                        : string.Format(CultureInfo.InvariantCulture, "height mix {0:0.000} (recorded {1:0.000}), heights at {2} repeats {3:0.000} (recorded {4:0.000})",
                                        rv, s.Verified, HeightRepeatsAlternative, r8, s.Heights8);
                    say("    fit " + f.Label + ", " + s.Name + ": " + nums8);
                    Verdict(problems8.Count == 0 && r8 > rv, f.Label + ", " + s.Name + ": heights sampled at " + HeightRepeatsAlternative +
                                                             " repeats instead of the layer's MTXP repeats fit worse (strictly)", nums8);
                }
                say(string.Format(CultureInfo.InvariantCulture, "    fit {0}: {1:0} ms", f.Label, sw.Elapsed.TotalMilliseconds));
            }
        }

        /// <summary>Root and tex0 named by the WDT's MAID, verified, built with the WDT's MPHD flags or, when
        /// `toggleHeights`, with 0x80 flipped and 0x4 set (the alpha stays 8-bit: the same flags as a real 0x80-clear
        /// 8-bit map). Null (and a FAIL when `what` is given) when an input is missing or changed.</summary>
        static TerrainTileMaterial BuildCorpusTile(string corpus, Pins pins, string client, uint wdtId, int x, int y, string rootDir, string texDir,
                                                   string rootSha, string texSha, bool toggleHeights, string what)
        {
            string wdtRel = "wdt/" + client + "/" + wdtId + ".bin";
            string problem = CheckPinned(corpus, pins, wdtRel);
            WdtFile wdt = problem == null ? WdtParser.Parse(File.ReadAllBytes(Path.Combine(corpus, wdtRel)), wdtRel) : null;
            WdtTileFiles files = default(WdtTileFiles);
            if (wdt != null && !(wdt.TileExists(x, y) && wdt.TryGetTileFiles(x, y, out files)))
                problem = "tile " + x + "_" + y + " has no MAID entry";
            if (problem != null)
            {
                if (what != null) Verdict(false, what + ": WDT " + wdtId + " has its pinned bytes and the tile", problem);
                return null;
            }
            string tag = what ?? client + " " + x + "_" + y;
            byte[] rootBytes = ReadInput(corpus, pins, rootDir ?? string.Format(Az, client), files[WdtTileFileRole.RootAdt], rootSha, null, tag + ": root", what != null);
            byte[] texBytes = ReadInput(corpus, pins, texDir ?? string.Format(Az, client), files[WdtTileFileRole.Tex0Adt], texSha, null, tag + ": tex0", what != null);
            if (rootBytes == null || texBytes == null)
                return null;
            return TerrainMaterialBuilder.Build(AdtParser.ParseRoot(rootBytes, x, y, tag + " root"), AdtParser.ParseTex(texBytes, tag + " tex0"),
                                                toggleHeights ? (wdt.Header.Flags ^ TerrainMaterialBuilder.HeightTextureMphdFlag) | 0x4u : wdt.Header.Flags);
        }

        /// <summary>
        /// The tile composite in map-texture sampling, 512 x 512 x RGB in 0..255 (north row first), with the
        /// compositor: every drawable chunk at 32 x 32 pixels, pixel centres, each layer texture and each height
        /// texture taken at its stored mip level log2(texels per map pixel) = log2(width * repeats / 32), which
        /// holds exactly one texel per pixel (else a problem is recorded), saturated (the 8-bit range).
        /// `heightRepeats` (0 = the layer's own) samples every height texture at that many repeats instead -- a
        /// fit alternative: each height texture at its level of one texel per pixel at those repeats, composed by
        /// ComposeGridHeightsAt (not by Compose, so the alternative stays at those repeats whatever Compose does).
        /// </summary>
        static float[] TileComposite(string corpus, Pins pins, string client, TerrainTileMaterial m, TerrainBlendRule rule, List<string> problems,
                                     int heightRepeats = 0)
        {
            var repeats = new Dictionary<uint, float>();
            var heightIds = new HashSet<uint>();
            var diffuseIds = new HashSet<uint>();
            foreach (TerrainChunkMaterial c in m.Chunks)
            {
                if (!c.Drawable)
                    continue;
                foreach (TerrainMaterialLayer l in c.Layers)
                {
                    uint diffuse = l.Missing ? 0u : l.TextureFileDataID;
                    uint height = rule == TerrainBlendRule.HeightMix && c.UseHeightTextures ? l.HeightTextureFileDataID : 0u;
                    if (diffuse != 0) diffuseIds.Add(diffuse);
                    if (height != 0) heightIds.Add(height);
                    foreach (uint id in new[] { diffuse, height })
                    {
                        if (id == 0)
                            continue;
                        float have;
                        if (repeats.TryGetValue(id, out have) && have != l.RepeatsPerChunk)
                            problems.Add("texture " + id + " is used at " + have.ToString(CultureInfo.InvariantCulture) + " and " +
                                         l.RepeatsPerChunk.ToString(CultureInfo.InvariantCulture) + " repeats");
                        repeats[id] = l.RepeatsPerChunk;
                    }
                }
            }
            if (heightRepeats != 0)
                foreach (uint id in heightIds)
                    if (diffuseIds.Contains(id))
                        problems.Add("texture " + id + " is both a diffuse and a height texture: the height-sampling alternative reads one level per id");
            var texels = new Dictionary<uint, TerrainTexels>();
            foreach (var kv in repeats)
            {
                string problem;
                byte[] b = ReadMaterialFile(corpus, pins, client, kv.Key, out problem);
                if (b == null)
                {
                    problems.Add(kv.Key + ": " + problem);
                    continue;
                }
                float at = heightRepeats != 0 && heightIds.Contains(kv.Key) ? heightRepeats : kv.Value;
                BlpHeader h = BlpDecoder.ReadHeader(b);
                double ratio = h.Width * (double)at / MapPixelsPerChunk;
                int level = ratio > 1 ? (int)Math.Round(Math.Log(ratio, 2)) : 0;
                if (level >= h.MipCount || h.Mips[level].Width * at != MapPixelsPerChunk || h.Mips[level].Height * at != MapPixelsPerChunk)
                {
                    problems.Add(string.Format(CultureInfo.InvariantCulture, "{0}: {1}x{2} at {3} repeats has no stored level of one texel per pixel",
                                               kv.Key, h.Width, h.Height, at));
                    continue;
                }
                BlpImage img = BlpDecoder.Decode(b, level);
                texels[kv.Key] = TerrainTexels.FromRgba32(img.Width, img.Height, img.Rgba);
            }
            var rgb = new float[MapSize * MapSize * 3];
            if (problems.Count > 0)
                return rgb;
            Func<uint, TerrainTexels> lookup = id => texels.TryGetValue(id, out TerrainTexels t) ? t : null;
            foreach (TerrainChunkMaterial c in m.Chunks)
            {
                if (!c.Drawable)
                    continue;
                float[] px = heightRepeats != 0 ? ComposeGridHeightsAt(c, lookup, heightRepeats, MapPixelsPerChunk)
                                                : TerrainCompositor.ComposeGrid(c, lookup, MapPixelsPerChunk, true, rule);
                int x0 = (c.Index % 16) * MapPixelsPerChunk, y0 = (c.Index / 16) * MapPixelsPerChunk;
                for (int y = 0; y < MapPixelsPerChunk; y++)
                    for (int x = 0; x < MapPixelsPerChunk; x++)
                        for (int ch = 0; ch < 3; ch++)
                            rgb[3 * ((y0 + y) * MapSize + x0 + x) + ch] = px[4 * (y * MapPixelsPerChunk + x) + ch] * 255f;
            }
            return rgb;
        }

        /// <summary>
        /// The fit alternative "heights at `heightRepeats` repeats": ComposeGrid's height mix (saturated, holes left 0)
        /// with every height texture read at s = (u, v) * heightRepeats / 8 instead of the layer's repeats, assembled
        /// here from the compositor's public pieces (AlphaAt, HeightMixWeights, LayerTexel, VertexColorAt, TexelIndex,
        /// IsHole) so that it does not depend on how Compose picks a height texture's repeats.
        /// </summary>
        static float[] ComposeGridHeightsAt(TerrainChunkMaterial c, Func<uint, TerrainTexels> textures, int heightRepeats, int size)
        {
            int n = c.Layers.Length;
            var px = new float[size * size * 4];
            var w = new float[n];
            var hv = new float[n];
            var weight = new float[n];
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    float u = (x + 0.5f) * 8f / size, v = (y + 0.5f) * 8f / size;
                    if (TerrainCompositor.IsHole(c.HoleMask, u, v))
                        continue;
                    float sum = 0f;
                    for (int k = 1; k < n; k++)
                    {
                        w[k] = TerrainCompositor.AlphaAt(c.Alpha, k, u, v);
                        sum += w[k];
                    }
                    w[0] = 1f - sum;
                    for (int k = 0; k < n; k++)
                    {
                        TerrainMaterialLayer l = c.Layers[k];
                        if (!c.UseHeightTextures)
                        {
                            hv[k] = 1f;
                            continue;
                        }
                        float h = 0f;
                        if (l.HeightTextureFileDataID != 0)
                        {
                            TerrainTexels t = textures(l.HeightTextureFileDataID);
                            float su = u * heightRepeats / 8f, sv = v * heightRepeats / 8f;
                            h = t.A[TerrainCompositor.TexelIndex(sv, t.Height) * t.Width + TerrainCompositor.TexelIndex(su, t.Width)];
                        }
                        hv[k] = h * l.HeightScale + l.HeightOffset;
                    }
                    TerrainCompositor.HeightMixWeights(w, hv, n, weight);
                    float cr = 0f, cg = 0f, cb = 0f;
                    for (int k = 0; k < n; k++)
                    {
                        float lr, lg, lb;
                        TerrainCompositor.LayerTexel(c.Layers[k], textures, u, v, out lr, out lg, out lb);
                        cr += weight[k] * lr;
                        cg += weight[k] * lg;
                        cb += weight[k] * lb;
                    }
                    float vr, vg, vb;
                    TerrainCompositor.VertexColorAt(c.VertexColors, u, v, out vr, out vg, out vb);
                    int o = 4 * (y * size + x);
                    px[o] = TerrainCompositor.Saturate(cr * vr);
                    px[o + 1] = TerrainCompositor.Saturate(cg * vg);
                    px[o + 2] = TerrainCompositor.Saturate(cb * vb);
                    px[o + 3] = 1f;
                }
            return px;
        }

        /// <summary>Per 4x4 block of the 512-px map (one cell): kept when its chunk passes `keep` and the cell is no hole.</summary>
        static bool[] KeepBlocks(TerrainTileMaterial m, Func<TerrainChunkMaterial, bool> keep)
        {
            const int nb = MapSize / 4;
            var k = new bool[nb * nb];
            foreach (TerrainChunkMaterial c in m.Chunks)
            {
                if (!keep(c))
                    continue;
                for (int by = 0; by < 8; by++)
                    for (int bx = 0; bx < 8; bx++)
                        if (((c.HoleMask >> (by * 8 + bx)) & 1UL) == 0)
                            k[((c.Index / 16) * 8 + by) * nb + (c.Index % 16) * 8 + bx] = true;
            }
            return k;
        }

        /// <summary>RMSE over the kept 4x4 blocks and the three channels of the block means (prediction 0..255 floats
        /// against the map texture's RGBA bytes).</summary>
        static double BlockRmse(float[] pred, byte[] mapRgba, bool[] keep)
        {
            const int nb = MapSize / 4;
            double sse = 0;
            int n = 0;
            for (int b = 0; b < keep.Length; b++)
            {
                if (!keep[b])
                    continue;
                int bx = b % nb, by = b / nb;
                for (int ch = 0; ch < 3; ch++)
                {
                    double p = 0, r = 0;
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                        {
                            int i = (by * 4 + y) * MapSize + bx * 4 + x;
                            p += pred[3 * i + ch];
                            r += mapRgba[4 * i + ch];
                        }
                    double d = (p - r) / 16.0;
                    sse += d * d;
                }
                n++;
            }
            return n == 0 ? double.NaN : Math.Sqrt(sse / (3.0 * n));
        }

        /// <summary>A texture file of `client` from this stage's verified extractions (materials/client/stage04_golden,
        /// stage04_reference, census_blp, stage04_nibble4, in that order): the copy whose SHA-256 and build are those of
        /// its folder's manifest_sha256.tsv, the build being the pinned client build. Null with the reason otherwise.</summary>
        static byte[] ReadMaterialFile(string corpus, Pins pins, string client, uint id, out string problem)
        {
            problem = "no verified local copy";
            string build;
            pins.Builds.TryGetValue(client, out build);
            foreach (string folder in new[] { "stage04_golden", "stage04_reference", "census_blp", "stage04_nibble4" })
            {
                string dir = Path.Combine(corpus, "materials", client, folder);
                string path = Path.Combine(dir, id + ".bin");
                if (!File.Exists(path))
                    continue;
                Dictionary<uint, string[]> rows = ReadManifestRows(Path.Combine(dir, "manifest_sha256.tsv"));
                string[] row;
                if (rows == null || !rows.TryGetValue(id, out row))
                {
                    problem = folder + ": not in the manifest";
                    continue;
                }
                byte[] b = File.ReadAllBytes(path);
                if (row[2] != Sha256(b) || row.Length < 4 || row[3] != build)
                {
                    problem = folder + ": changed, or not the pinned build " + build;
                    continue;
                }
                problem = folder;
                return b;
            }
            return null;
        }

        // ================================================================================== BLP headers

        static void BlpHeaders(string corpus)
        {
            // Census section 9 (formats as the census lists them): FileDataID -> (client, encoding, width, levels).
            var expect = new[]
            {
                new { Client = "wow", Id = 186770u, Enc = "dxt3", Size = 256, Levels = 9 },
                new { Client = "wow", Id = 186947u, Enc = "dxt5", Size = 256, Levels = 9 },
                new { Client = "wow_classic_era", Id = 186947u, Enc = "dxt3", Size = 256, Levels = 9 },
                new { Client = "wow", Id = 188903u, Enc = "dxt1", Size = 8, Levels = 4 },
                new { Client = "wow_classic_beta", Id = 188903u, Enc = "dxt1", Size = 32, Levels = 6 },
                new { Client = "wow", Id = 971416u, Enc = "dxt5", Size = 1024, Levels = 11 },
                new { Client = "wow", Id = 187901u, Enc = "dxt5", Size = 256, Levels = 9 },
            };
            foreach (string client in new[] { "wow", "wow_classic_era", "wow_classic_beta" })
            {
                string dir = Path.Combine(corpus, "materials", client, "census_blp");
                Dictionary<uint, string> manifest = ReadManifest(Path.Combine(dir, "manifest_sha256.tsv"));
                if (manifest == null)
                {
                    Verdict(false, client + ": census_blp manifest_sha256.tsv present");
                    continue;
                }
                int files = 0, exact = 0, decoded = 0;
                var problems = new List<string>();
                var formats = new SortedDictionary<string, int>(StringComparer.Ordinal);
                foreach (var e in manifest)
                {
                    string path = Path.Combine(dir, e.Key + ".bin");
                    if (!File.Exists(path)) continue;
                    byte[] b = File.ReadAllBytes(path);
                    files++;
                    if (Sha256(b) != e.Value) { problems.Add(e.Key + " changed"); continue; }
                    BlpHeader h;
                    try { h = BlpDecoder.ReadHeader(b); }
                    catch (WowParseException x) { problems.Add(e.Key + ": " + x.Message); continue; }
                    string f = h.Encoding + " " + h.Width + "x" + h.Height + "/" + h.MipCount;
                    formats[f] = formats.TryGetValue(f, out int n) ? n + 1 : 1;
                    bool chain = h.MipCount == h.FullChainLength && h.Mips[0].Offset == 0x494;
                    for (int l = 0; l < h.MipCount; l++)
                        chain &= h.Mips[l].StoredSize == h.Mips[l].DataSize && (l == 0 || h.Mips[l].Offset == h.Mips[l - 1].Offset + h.Mips[l - 1].StoredSize);
                    chain &= h.Mips[h.MipCount - 1].Offset + h.Mips[h.MipCount - 1].StoredSize == b.Length;
                    if (chain) exact++;
                    else problems.Add(e.Key + ": chain not exact (" + f + ")");
                    BlpImage first = BlpDecoder.Decode(b, 0), last = BlpDecoder.Decode(b, h.MipCount - 1);
                    if (first.Width == h.Width && first.Height == h.Height && last.Width == h.Mips[h.MipCount - 1].Width && first.Encoding == h.Encoding)
                        decoded++;
                    else
                        problems.Add(e.Key + ": Decode disagrees");
                    foreach (var x in expect.Where(x => x.Client == client && x.Id == e.Key))
                        if (h.Encoding != x.Enc || h.Width != x.Size || h.Height != x.Size || h.MipCount != x.Levels || !h.IsBlockCompressed)
                            problems.Add(e.Key + ": " + f + ", the census lists " + x.Enc + " " + x.Size + "/" + x.Levels);
                }
                Verdict(files > 0 && exact == files && decoded == files && problems.Count == 0,
                        client + ": BLP header and mip table of every extracted terrain BLP (format as the census lists it, full chain, levels back to back and exactly sized, Decode agrees)",
                        files + " files, " + exact + " exact chains; " + string.Join(", ", formats.Select(p => p.Key + " x" + p.Value)) +
                        (problems.Count > 0 ? "; " + string.Join("; ", problems.Take(5)) : ""));
            }
        }

        // ================================================================================== inputs

        static byte[] ReadInput(string corpus, Pins pins, string dir, uint id, string sha, string sha12, string what, bool report = true)
        {
            string rel = dir + "/" + id + ".bin";
            string path = Path.Combine(corpus, rel);
            string problem;
            if (sha == null)
                problem = CheckPinned(corpus, pins, rel);
            else if (!File.Exists(path))
                problem = "missing " + rel;
            else
            {
                Dictionary<uint, string[]> manifest = ReadManifestRows(Path.Combine(corpus, dir, "manifest_sha256.tsv"));
                string[] row = null;
                if (manifest != null) manifest.TryGetValue(id, out row);
                string actual = Sha256(File.ReadAllBytes(path));
                string build = null, owner = ClientOf(dir);
                if (owner != null) pins.Builds.TryGetValue(owner, out build);
                problem = actual != sha ? "changed " + rel + " (pinned sha256 " + sha.Substring(0, 16) + ")"
                        : row == null || row[2] != sha ? rel + ": the extraction manifest does not list this SHA-256"
                        : row.Length >= 4 && build != null && row[3] != build ? rel + ": the extraction manifest names build " + row[3] + ", not the pinned " + build : null;
            }
            byte[] bytes = problem == null ? File.ReadAllBytes(path) : null;
            if (bytes != null && sha12 != null && !Sha256(bytes).StartsWith(sha12, StringComparison.Ordinal))
                problem = rel + " is not the census file (" + sha12 + ")";
            if (report || problem != null)
                Verdict(problem == null, what + " " + id + " has its pinned bytes" + (sha12 != null ? " (census " + sha12 + ")" : ""),
                        problem ?? (bytes.Length + " B, sha256 " + Sha256(bytes).Substring(0, 16)));
            return problem == null ? bytes : null;
        }

        /// <summary>The client folder of a corpus-relative "materials/<client>/..." path, or null.</summary>
        static string ClientOf(string dir)
        {
            string[] parts = dir.Split('/');
            return parts.Length >= 2 && parts[0] == "materials" ? parts[1] : null;
        }

        /// <summary>id -> the row's columns (id, bytes, sha256, build[, status]) from an extraction's manifest_sha256.tsv.</summary>
        static Dictionary<uint, string[]> ReadManifestRows(string path)
        {
            if (!File.Exists(path))
                return null;
            var d = new Dictionary<uint, string[]>();
            foreach (string line in File.ReadAllLines(path))
            {
                string[] p = line.Split('\t');
                uint id;
                if (p.Length >= 3 && uint.TryParse(p[0], NumberStyles.None, CultureInfo.InvariantCulture, out id))
                    d[id] = p;
            }
            return d;
        }

        /// <summary>id -> sha256 from an extraction's manifest_sha256.tsv (first column the id, third the SHA-256).</summary>
        static Dictionary<uint, string> ReadManifest(string path)
        {
            if (!File.Exists(path))
                return null;
            var d = new Dictionary<uint, string>();
            foreach (string line in File.ReadAllLines(path))
            {
                string[] p = line.Split('\t');
                uint id;
                if (p.Length >= 3 && uint.TryParse(p[0], NumberStyles.None, CultureInfo.InvariantCulture, out id))
                    d[id] = p[2];
            }
            return d;
        }

        sealed class Pins
        {
            public Dictionary<string, string> Files = new Dictionary<string, string>(StringComparer.Ordinal);
            public Dictionary<string, KeyValuePair<int, string>> Sets = new Dictionary<string, KeyValuePair<int, string>>(StringComparer.Ordinal);
            public Dictionary<string, bool> SetVerified = new Dictionary<string, bool>(StringComparer.Ordinal);
            public Dictionary<string, string> Builds = new Dictionary<string, string>(StringComparer.Ordinal);   // client -> pinned build
        }

        static string FindPins()
        {
            string env = Environment.GetEnvironmentVariable("WMV_WORLD_AUDIT_PINS");
            if (!string.IsNullOrEmpty(env) && File.Exists(env))
                return env;
            foreach (string start in new[] { Environment.CurrentDirectory, AppContext.BaseDirectory })
            {
                var d = new DirectoryInfo(start);
                while (d != null)
                {
                    string p = Path.Combine(d.FullName, "Tools", "world-audit", "tests", "corpus_hashes.json");
                    if (File.Exists(p))
                        return p;
                    d = d.Parent;
                }
            }
            return null;
        }

        static Pins LoadPins(string path)
        {
            var pins = new Pins();
            using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path)))
            {
                foreach (JsonProperty f in doc.RootElement.GetProperty("files").EnumerateObject())
                    pins.Files[f.Name] = f.Value.GetString();
                foreach (JsonProperty s in doc.RootElement.GetProperty("sets").EnumerateObject())
                    pins.Sets[s.Name] = new KeyValuePair<int, string>(s.Value.GetProperty("files").GetInt32(), s.Value.GetProperty("digest").GetString());
                foreach (JsonProperty b in doc.RootElement.GetProperty("client_builds").EnumerateObject())
                    pins.Builds[b.Name] = b.Value.GetString();
            }
            return pins;
        }

        /// <summary>Null when the corpus file at `rel` has its own pinned SHA-256 or lies in a pinned file set
        /// whose digest holds (the digest rule of TerrainCorpusTests); otherwise what is wrong.</summary>
        static string CheckPinned(string corpus, Pins pins, string rel)
        {
            string path = Path.Combine(corpus, rel);
            if (!File.Exists(path))
                return "missing " + rel;
            string want;
            if (pins.Files.TryGetValue(rel, out want))
                return Sha256(File.ReadAllBytes(path)) == want ? null : "changed " + rel + " (pinned sha256 " + want.Substring(0, 16) + ")";
            string dir = rel.Substring(0, rel.LastIndexOf('/'));
            KeyValuePair<int, string> set;
            if (!pins.Sets.TryGetValue(dir, out set))
                return rel + " is not pinned";
            bool verified;
            if (!pins.SetVerified.TryGetValue(dir, out verified))
            {
                string full = Path.Combine(corpus, dir);
                var names = Directory.GetFiles(full, "*.bin").Select(Path.GetFileName).OrderBy(n => n, StringComparer.Ordinal).ToList();
                var sb = new StringBuilder();
                foreach (string n in names) sb.Append(n).Append(' ').Append(Sha256(File.ReadAllBytes(Path.Combine(full, n)))).Append('\n');
                string digest = Sha256(Encoding.ASCII.GetBytes(sb.ToString()));
                verified = names.Count == set.Key && digest == set.Value;
                pins.SetVerified[dir] = verified;
                say(string.Format("    pinned set {0}: {1} files, digest {2}", dir, names.Count, verified ? "holds" : "CHANGED"));
            }
            return verified ? null : "the pinned set " + dir + " changed";
        }

        static string Sha256(byte[] b)
        {
            using (var s = SHA256.Create())
                return Convert.ToHexString(s.ComputeHash(b)).ToLowerInvariant();
        }

        static string Sha12(byte[] b) { return Sha256(b).Substring(0, 12); }

        static double Median(List<double> v)
        {
            var s = v.OrderBy(x => x).ToList();
            return s.Count % 2 == 1 ? s[s.Count / 2] : (s[s.Count / 2 - 1] + s[s.Count / 2]) / 2;
        }
    }
}
