// BlpBlockTests.cs
//
// Stage 08 (docs/world-viewer/08-streaming.md 1.9): the block-compressed upload's flip, BlpBlocks. Every flipped level
// decodes (BlpDecoder, exact block mode) to the file's own level with its rows reversed -- synthetic DXT1, DXT3 and DXT5
// files of every level height down to 1 x 1, both DXT5 alpha modes, then (Run, with WMV_WORLD_AUDIT_CORPUS set) every
// whole BLP2 file of the Stage 07 corpus on the four clients -- and Unsupported names the rule a file breaks.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Wmv.Wow.Tests
{
    public static class BlpBlockTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static void PutU32(byte[] b, int o, uint v)
        {
            b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 16); b[o + 3] = (byte)(v >> 24);
        }

        /// <summary>A BLP2 file with the given levels back to back from 0x494.</summary>
        static byte[] Blp(int encoding, int alphaDepth, int preferred, int w, int h, List<byte[]> mips)
        {
            int total = 0x494;
            foreach (byte[] m in mips) total += m.Length;
            var b = new byte[total];
            b[0] = (byte)'B'; b[1] = (byte)'L'; b[2] = (byte)'P'; b[3] = (byte)'2';
            PutU32(b, 4, 1);
            b[8] = (byte)encoding; b[9] = (byte)alphaDepth; b[10] = (byte)preferred; b[11] = (byte)(mips.Count > 1 ? 2 : 0);
            PutU32(b, 12, (uint)w); PutU32(b, 16, (uint)h);
            int at = 0x494;
            for (int m = 0; m < mips.Count; m++)
            {
                PutU32(b, 0x14 + 4 * m, (uint)at);
                PutU32(b, 0x54 + 4 * m, (uint)mips[m].Length);
                Buffer.BlockCopy(mips[m], 0, b, at, mips[m].Length);
                at += mips[m].Length;
            }
            return b;
        }

        /// <summary>DXT1 (1), DXT3 (3) or DXT5 (5) of w x h with `levels` stored levels of random blocks.</summary>
        static byte[] DxtFile(int kind, int w, int h, int levels, Random rng)
        {
            int block = kind == 1 ? 8 : 16;
            var mips = new List<byte[]>();
            for (int m = 0; m < levels; m++)
            {
                int mw = Math.Max(1, w >> m), mh = Math.Max(1, h >> m);
                var d = new byte[Math.Max(1, (mw + 3) / 4) * Math.Max(1, (mh + 3) / 4) * block];
                rng.NextBytes(d);
                mips.Add(d);
            }
            return kind == 1 ? Blp(2, 0, 0, w, h, mips) : Blp(2, 8, kind == 3 ? 1 : 7, w, h, mips);
        }

        static byte[] RowsFlipped(BlpImage img)
        {
            var o = new byte[img.Rgba.Length];
            int row = img.Width * 4;
            for (int y = 0; y < img.Height; y++)
                Buffer.BlockCopy(img.Rgba, y * row, o, (img.Height - 1 - y) * row, row);
            return o;
        }

        /// <summary>Level m of the chain decodes to the file's level m upside down: the chain's bytes put in the file's
        /// place of level m, decoded, against the file's own level decoded and its rows reversed.</summary>
        static bool LevelMatches(byte[] file, BlpHeader h, BlpBlockChain c, int m, out string detail)
        {
            var copy = (byte[])file.Clone();
            Buffer.BlockCopy(c.Data, c.LevelOffsets[m], copy, h.Mips[m].Offset, c.LevelSizes[m]);
            byte[] want = RowsFlipped(BlpDecoder.Decode(file, m, true));
            byte[] got = BlpDecoder.Decode(copy, m, true).Rgba;
            for (int i = 0; i < want.Length; i++)
                if (want[i] != got[i])
                {
                    detail = string.Format("level {0} ({1}x{2}): byte {3} is {4}, want {5}", m, h.Mips[m].Width, h.Mips[m].Height, i, got[i], want[i]);
                    return false;
                }
            detail = "";
            return true;
        }

        static void FlipTests()
        {
            var rng = new Random(8);
            int files = 0, levels = 0, bad = 0;
            string firstBad = "";
            foreach (int kind in new[] { 1, 3, 5 })
                foreach (int[] size in new[] { new[] { 4, 4 }, new[] { 8, 8 }, new[] { 16, 8 }, new[] { 8, 16 }, new[] { 32, 4 },
                                               new[] { 4, 32 }, new[] { 64, 64 }, new[] { 256, 64 } })
                    for (int seed = 0; seed < 4; seed++)
                    {
                        int full = BlpDecoder.FullChainLength(size[0], size[1]);
                        byte[] file = DxtFile(kind, size[0], size[1], full, rng);
                        BlpHeader h = BlpDecoder.ReadHeader(file);
                        BlpBlockChain c = BlpBlocks.FlippedChain(file, h);
                        files++;
                        for (int m = 0; m < h.MipCount; m++)
                        {
                            levels++;
                            string d;
                            if (!LevelMatches(file, h, c, m, out d))
                            {
                                if (bad == 0) firstBad = "dxt" + kind + " " + size[0] + "x" + size[1] + " " + d;
                                bad++;
                            }
                        }
                    }
            Check(bad == 0, string.Format("flip: every level of {0} synthetic DXT1/DXT3/DXT5 files ({1} levels, heights 64 down to 1, " +
                                          "both DXT5 alpha modes) decodes to the file's level upside down{2}", files, levels,
                                          bad == 0 ? "" : " -- " + bad + " differ, first " + firstBad));

            // Twice is the identity, at every row count a block can show (4, 2, 1).
            bool involution = true;
            foreach (int kind in new[] { 1, 3, 5 })
                foreach (int height in new[] { 4, 2, 1 })
                {
                    BlpPixelFormat f = kind == 1 ? BlpPixelFormat.Dxt1 : kind == 3 ? BlpPixelFormat.Dxt3 : BlpPixelFormat.Dxt5;
                    var block = new byte[BlpBlocks.BlockBytes(f)];
                    rng.NextBytes(block);
                    var a = new byte[block.Length];
                    var b = new byte[block.Length];
                    BlpBlocks.FlipLevel(block, 0, a, 0, 4, height, f);
                    BlpBlocks.FlipLevel(a, 0, b, 0, 4, height, f);
                    involution &= b.SequenceEqual(block);
                    if (height == 1)
                        involution &= a.SequenceEqual(block);   // one row: nothing moves
                }
            Check(involution, "flip: flipping a block twice gives it back (4, 2 and 1 rows); one row is left as it is");

            // The chain: every level once, back to back, at its D3D size.
            byte[] f256 = DxtFile(5, 256, 64, 9, rng);
            BlpHeader h256 = BlpDecoder.ReadHeader(f256);
            BlpBlockChain c256 = BlpBlocks.FlippedChain(f256, h256);
            long sum = 0;
            bool laidOut = c256.Levels == 9 && c256.LevelOffsets[0] == 0 && c256.Format == BlpPixelFormat.Dxt5 && c256.Width == 256 &&
                           c256.Height == 64;
            for (int m = 0; m < 9; m++)
            {
                laidOut &= c256.LevelOffsets[m] == sum && c256.LevelSizes[m] == h256.Mips[m].DataSize;
                sum += c256.LevelSizes[m];
            }
            Check(laidOut && c256.Bytes == sum, string.Format("chain: 9 levels back to back at their D3D sizes, {0} bytes", c256.Bytes));
        }

        static void RuleTests()
        {
            var rng = new Random(9);
            Func<byte[], string> why = f => BlpBlocks.Unsupported(BlpDecoder.ReadHeader(f));
            Check(why(DxtFile(1, 64, 64, 7, rng)) == null && why(DxtFile(3, 32, 8, 6, rng)) == null && why(DxtFile(5, 4, 4, 3, rng)) == null,
                  "rules: a full chain of whole blocks is supported (DXT1, DXT3, DXT5; down to 4 x 4)");
            string partial = why(DxtFile(5, 256, 256, 1, rng));
            Check(partial != null && partial.Contains("1 of the 9 levels"), "rules: a file of one stored level is not (" + partial + ")");
            string twelve = why(DxtFile(1, 12, 12, 4, rng));
            Check(twelve != null && twelve.Contains("level 1 is 6 pixels high"), "rules: 12 x 12 is not -- its level 1 is 6 high (" + twelve + ")");
            var pal = new List<byte[]> { new byte[64 + 64] };
            string palettized = why(Blp(1, 8, 0, 8, 8, pal));
            Check(palettized != null && palettized.Contains("not block-compressed"), "rules: a palettized file is not (" + palettized + ")");
            bool threw = false;
            try { BlpBlocks.FlippedChain(DxtFile(1, 12, 12, 4, rng), BlpDecoder.ReadHeader(DxtFile(1, 12, 12, 4, rng))); }
            catch (ArgumentException) { threw = true; }
            Check(threw, "rules: FlippedChain refuses a file Unsupported refuses");
        }

        static void PrepareTests()
        {
            var rng = new Random(10);
            byte[] full = DxtFile(5, 64, 32, 7, rng);
            BlpImage blocks = BlpBlocks.PrepareForModels(full, f => true);
            BlpHeader h = BlpDecoder.ReadHeader(full);
            Check(blocks.Blocks != null && blocks.Rgba == null && blocks.Width == 64 && blocks.Height == 32 && blocks.Encoding == "dxt5" &&
                  blocks.HasAlpha && blocks.BlocksNotUsed == null && blocks.HeldBytes == blocks.Blocks.Bytes &&
                  BlpBlocks.UploadBytes(blocks) == blocks.Blocks.Bytes && blocks.Blocks.Data.SequenceEqual(BlpBlocks.FlippedChain(full, h).Data),
                  "prepare: a full-chain DXT5 file the GPU samples is its own blocks, flipped, " + blocks.Blocks.Bytes + " bytes held");
            BlpImage noGpu = BlpBlocks.PrepareForModels(full, f => false);
            Check(noGpu.Blocks == null && noGpu.Rgba != null && noGpu.BlocksNotUsed == "this GPU does not sample dxt5" &&
                  noGpu.Rgba.SequenceEqual(BlpDecoder.Decode(full).Rgba),
                  "prepare: one the GPU does not sample is decoded as before (" + noGpu.BlocksNotUsed + ")");
            BlpImage partial = BlpBlocks.PrepareForModels(DxtFile(1, 256, 256, 1, rng), f => true);
            Check(partial.Blocks == null && partial.Rgba != null && partial.BlocksNotUsed.Contains("1 of the 9 levels") &&
                  BlpBlocks.UploadBytes(partial) == BlpBlocks.Rgba32ChainBytes(256, 256) && BlpBlocks.Rgba32ChainBytes(256, 256) == 349524L,
                  "prepare: a file of one stored level is decoded; its upload is RGBA32 with its full chain (349,524 bytes for 256 x 256)");
            Check(BlpBlocks.Rgba32ChainBytes(4, 1) == 4 * 4 + 2 * 4 + 4 && BlpBlocks.Rgba32ChainBytes(1, 1) == 4,
                  "prepare: the RGBA32 chain of a non-square size runs to 1 x 1 (4 x 1: 28 bytes)");
            // A DXT1 16 x 4 whose level 1 (8 x 2: one row of two blocks, 16 bytes) stores 8 bytes: the header's table fails, level
            // 0 still decodes -- prepared decoded, as before Stage 08, not failed.
            var level0 = new byte[32];
            rng.NextBytes(level0);
            var mips = new List<byte[]> { level0, new byte[8], new byte[8], new byte[8], new byte[8] };
            byte[] shortMip = Blp(2, 0, 0, 16, 4, mips);
            bool headerFails = false;
            try { BlpDecoder.ReadHeader(shortMip); }
            catch (WowParseException) { headerFails = true; }
            BlpImage shortImg = null;
            string thrown = null;
            try { shortImg = BlpBlocks.PrepareForModels(shortMip, f => true); }
            catch (Exception e) { thrown = e.Message; }
            Check(headerFails && thrown == null && shortImg != null && shortImg.Blocks == null && shortImg.Rgba != null &&
                  shortImg.Rgba.SequenceEqual(BlpDecoder.Decode(shortMip).Rgba) && shortImg.BlocksNotUsed.Contains("mip 1 holds 8 bytes, needs 16"),
                  "prepare: a file whose small mip is shorter than its size needs is decoded from level 0 as before, not failed (" +
                  (thrown ?? (shortImg != null ? shortImg.BlocksNotUsed : "null")) + ")");
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            Action[] tests = { FlipTests, RuleTests, PrepareTests };
            foreach (Action t in tests)
            {
                try { t(); }
                catch (Exception e) { Check(false, t.Method.Name + " threw " + e.GetType().Name + ": " + e.Message); }
            }
            if (output != null)
            {
                foreach (string line in log) output(line);
                output(string.Format("BlpBlockTests: {0} failure(s)", failures));
            }
            return failures;
        }

        /// <summary>The decoder itself cannot read the file's level 0 (then preparing it may throw too).</summary>
        static bool BlpDecoderFails(byte[] file)
        {
            try { BlpDecoder.Decode(file); return false; }
            catch (WowParseException) { return true; }
        }

        /// <summary>Every whole BLP2 file of the Stage 07 corpus (world07/client/*.bin): the supported ones flipped and every
        /// level checked against the decoder; the others counted by the rule they break.</summary>
        public static int Run(string corpus, Action<string> output)
        {
            failures = 0;
            log.Clear();
            string root = Path.Combine(corpus, "world07");
            foreach (string client in new[] { "wow", "wow_classic_era", "wow_classic", "wow_classic_beta" })
            {
                string dir = Path.Combine(root, client);
                if (!Directory.Exists(dir))
                {
                    log.Add("  SKIP  " + client + ": no " + dir);
                    continue;
                }
                int files = 0, supported = 0, levels = 0, bad = 0, tableFails = 0;
                string firstBad = "";
                var reasons = new Dictionary<string, int>();
                foreach (string path in Directory.GetFiles(dir, "*.bin").OrderBy(p => p, StringComparer.Ordinal))
                {
                    byte[] file = File.ReadAllBytes(path);
                    if (file.Length < 0x494 || file[0] != (byte)'B' || file[1] != (byte)'L' || file[2] != (byte)'P' || file[3] != (byte)'2')
                        continue;
                    BlpHeader h;
                    try { h = BlpDecoder.ReadHeader(file); }
                    catch (WowParseException)
                    {
                        // The header's table fails (a short small mip): prepared from level 0, as the decoder reads it.
                        string got;
                        try
                        {
                            BlpImage d = BlpBlocks.PrepareForModels(file, f => true);
                            got = d.Rgba != null && d.Rgba.SequenceEqual(BlpDecoder.Decode(file).Rgba) ? null : "differs from the decoder's level 0";
                        }
                        catch (WowParseException e) { got = BlpDecoderFails(file) ? null : "throws " + e.Message; }
                        tableFails++;
                        if (got != null)
                        {
                            if (bad == 0) firstBad = Path.GetFileName(path) + " " + got;
                            bad++;
                        }
                        continue;
                    }
                    files++;
                    string why = BlpBlocks.Unsupported(h);
                    if (why != null)
                    {
                        string k = !h.IsBlockCompressed ? "not block-compressed" : h.MipCount < h.FullChainLength ? "mip chain not stored whole" : "rows";
                        int n;
                        reasons[k] = reasons.TryGetValue(k, out n) ? n + 1 : 1;
                        continue;
                    }
                    supported++;
                    BlpBlockChain c = BlpBlocks.FlippedChain(file, h);
                    for (int m = 0; m < h.MipCount; m++)
                    {
                        levels++;
                        string d;
                        if (!LevelMatches(file, h, c, m, out d))
                        {
                            if (bad == 0) firstBad = Path.GetFileName(path) + " " + h.Encoding + " " + d;
                            bad++;
                        }
                    }
                }
                Check(files > 0 && supported > 0 && bad == 0,
                      string.Format("{0}: {1} BLP2 files, {2} supported, every one of their {3} levels decodes to the file's level " +
                                    "upside down{4}; not supported: {5}; {6} more whose mip table fails, prepared from level 0 as the " +
                                    "decoder reads it", client, files, supported, levels,
                                    bad == 0 ? "" : " -- " + bad + " differ, first " + firstBad,
                                    reasons.Count == 0 ? "none" : string.Join(", ", reasons.Select(kv => kv.Value + " " + kv.Key)), tableFails));
            }
            foreach (string line in log) output(line);
            output(string.Format("BlpBlockCorpusTests: {0} failure(s)", failures));
            return failures;
        }
    }
}
