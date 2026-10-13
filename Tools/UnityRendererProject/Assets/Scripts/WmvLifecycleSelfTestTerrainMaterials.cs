// WmvLifecycleSelfTestTerrainMaterials.cs
//
// The stage 04 part of the player's self-test (-wmvLifecycleTest; WmvLifecycleSelfTest.TerrainTests runs it): the
// terrain TEXTURE MATERIAL through the real player code, on synthetic bytes whose every value is known.
//
//   - textures: BLPs of every encoding the player meets (raw BGRA, palettized, DXT1, DXT3, DXT5) prepared and uploaded
//     by WmvTerrainTexture, then read back from the GPU by Load (the texel dump's path): each STORED mip as stored
//     (rows not flipped; a stored mip that is not a filter of the one above shows the file's own mips are used), the
//     raw and palettized texels exactly; a block-compressed file's endpoint colours exactly the bit-replicating decode
//     of its stored blocks, its alpha exactly where the format makes it exact (DXT1, DXT3's nibble x 17, DXT5's a0, a1,
//     0 and 255), and its interpolated colours exactly ONE named decode -- the measured hardware model (5-bit
//     ((2a + b) * 22) >> 3 on the stored values, 6-bit (256 a + trunc(d / 4) + 128 + 80 d) >> 8 on the widened ones)
//     or the exact ramp -- (DXT5's interpolated alpha measured and logged); the formats, the mip counts, the sampler
//     settings (mip bias 0 among them), the CPU copy gone; md5, decode and block-size failures; a block-compressed file
//     taken as RGBA32 (BlpDecoder's exact block mode) equal to the file's own independent block decode (endpoints and
//     exact alphas exact, interpolants the exact value rounded), the 6x6 DXT5 included;
//   - the composite, THE HEIGHT MIX (v3 delta section 1): synthetic tiles with MPHD 0x80 (height textures and MTXP
//     heights in the mix: a DXT5 and two raw height textures, heights that reach 0 so S = 0 falls back to the raw
//     weights; height textures at every layer index 0..4, chunk 18 at 2, 3 and 4) and without (H = 1), 1 to 5 layers, RLE
//     and raw alpha, MCCV, every texture scale 0..3, a missing texture, a static layer effect and holes -- and a second
//     tex0 over the same root: EIGHT layers with a height texture and MTXP heights on each (layers 5..7 from the second
//     alpha slice), SIX layers, MTXP scale nibble 4 (0.5 repeats) at the UV phases (0.5, 0), (0.5, 0.5) and (0, 0.5) (the
//     phase and the u/v order decide), and a missing layer with a height texture (its slot holds the tile's sampler
//     carrier, whose sampler that height texture is read with: the production render fits the CPU with it and not with a
//     Clamp carrier); every drawable chunk rendered alone at the design's validation size by the composite renderer (a
//     CommandBuffer into a float target) and compared at every pixel with TerrainCompositor (the CPU twin, fed the GPU's
//     own texels as V1 is): within 0.5/255 in validation sampling, 2/255 in production sampling with every texture at one
//     texel per pixel (three and six layers); the plain weights and the heights-off reading both far off where the
//     heights decide; the production clamp and its clippedPixels count against the CPU; the plain-weights diagnostic
//     against the CPU's plain weights; holes cleared, every other pixel covered; the identity the only one of the 8
//     symmetries that fits; the whole tile's chunk placement at 32 pixels per chunk;
//   - the dumps: refused outside the textured view; _WmvLinearOut, _WmvHatch and the view pinned to the authored
//     composite for the render and put back; the tile composite and the texel dump streamed in bands, equal to one
//     render and to the whole read; the sidecar's build and md5s; the texel dump's build, md5s and roles;
//   - every state of the v3 delta section 2 drawn as specified in every view (textured, texturedlit,
//     texturedplainweights, materials, shaded) -- which shader, which values, which textures, heights and slices --, the
//     status and capture label from what was drawn (-wmvTerrainChunk's chunk named when it alone is neutral or has no
//     triangles), every layer slot's values k = 0..7 packed into the material, an upload failure leaving only the
//     textures the remaining materials bind, a static chunk with a failed texture (state kept, checker counted), a layer
//     of unresolved repeats drawn as the checker, and the disposal of everything back to zero;
//   - the session with textures against a fake host: K = 4 (the WDT alone, then the root and the tex0, then textures; a
//     texture counted until its upload data are ready), height textures asked on an MPHD 0x80 map only and reported
//     under their role, a failed height texture making its chunks HeightTextureMissing, a height texture without an
//     alpha channel failing in its height role only, a load ended before adoption reporting its answered failures
//     settled and its unanswered textures counted, every pending id retired at a load end and named in the report,
//     late texture answers dropped, a client switch and a timeout with textures out,
//     the per-load cap, every texture state, a tex0 that fails, a protocol-12 host, the ring past 128, conservation and
//     no growing collection over many loads;
//   - the protocol-13 lines (loadTerrainTile's limits, terrainTileLoaded's material with roles, runtimeState's texture
//     fields).

using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using Wmv.Wow;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    static void TerrainMaterialTests(Action<string> log)
    {
        if (WmvTerrainMaterials.ResolveShader(null) == null)
        {
            Check(false, "terrain material: Resources/WmvTerrainTextured.shader ships in the player and is supported", log);
            return;
        }
        Check(true, "terrain material: Resources/WmvTerrainTextured.shader ships in the player (" + WmvTerrainMaterials.ShaderName + "); " +
              WmvTerrainFormatSupport.Query(), log);
        float encode = Shader.GetGlobalFloat("_WmvShaderEncode");
        try
        {
            TerrainTextureUploadTests(log);
            TerrainCompositeTests(log);
            TerrainLayersPhaseCarrierTests(log);
            TerrainDumpTests(log);
            TerrainMaterialViewTests(log);
            TerrainTextureSessionTests(log);
            TerrainMaterialIpcTests(log);
        }
        catch (Exception e)
        {
            Check(false, "terrain material: the tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log);
        }
        finally
        {
            Shader.SetGlobalFloat("_WmvShaderEncode", encode);
        }
    }

    // ================================================================ synthetic BLPs

    const int BlpHeaderBytes = 0x494;
    /// <summary>How close (1/255) a texel read back from a float32 target must be to call two decodes equal.</summary>
    const float TexelExact = 0.01f;

    /// <summary>A BLP2 file: the header, the palette (when given) and the stored mips back to back from 0x494.</summary>
    static byte[] Blp(int encoding, int alphaSize, int preferredFormat, int width, int height, byte[] palette, List<byte[]> mips)
    {
        int total = BlpHeaderBytes;
        foreach (byte[] m in mips) total += m.Length;
        var b = new byte[total];
        b[0] = (byte)'B'; b[1] = (byte)'L'; b[2] = (byte)'P'; b[3] = (byte)'2';
        AdtSynthetic.PutU32(b, 4, 1);
        b[8] = (byte)encoding;
        b[9] = (byte)alphaSize;
        b[10] = (byte)preferredFormat;
        b[11] = (byte)(mips.Count > 1 ? 1 : 0);
        AdtSynthetic.PutU32(b, 12, (uint)width);
        AdtSynthetic.PutU32(b, 16, (uint)height);
        int at = BlpHeaderBytes;
        for (int m = 0; m < mips.Count; m++)
        {
            AdtSynthetic.PutU32(b, 0x14 + 4 * m, (uint)at);
            AdtSynthetic.PutU32(b, 0x54 + 4 * m, (uint)mips[m].Length);
            Buffer.BlockCopy(mips[m], 0, b, at, mips[m].Length);
            at += mips[m].Length;
        }
        if (palette != null)
            Buffer.BlockCopy(palette, 0, b, 0x94, Math.Min(1024, palette.Length));
        return b;
    }

    static int Levels(int w, int h) { return BlpDecoder.FullChainLength(w, h); }

    /// <summary>A deterministic byte of (seed, a, b, c): a texture's asymmetric pattern.</summary>
    static byte Hash(int seed, int a, int b, int c)
    {
        unchecked
        {
            uint x = (uint)(seed * 73856093) ^ (uint)(a * 19349663) ^ (uint)(b * 83492791) ^ (uint)(c * 2654435761u);
            x ^= x >> 13;
            x *= 0x5bd1e995;
            x ^= x >> 15;
            return (byte)x;
        }
    }

    /// <summary>Raw BGRA (encoding 3), every stored level its own pattern (never a filter of the one above).</summary>
    static byte[] RawBlp(int seed, int w, int h, int levels = -1)
    {
        if (levels < 0) levels = Levels(w, h);
        var mips = new List<byte[]>();
        for (int m = 0; m < levels; m++)
        {
            int mw = Math.Max(1, w >> m), mh = Math.Max(1, h >> m);
            var d = new byte[mw * mh * 4];
            for (int y = 0; y < mh; y++)
                for (int x = 0; x < mw; x++)
                    for (int c = 0; c < 4; c++)
                        d[4 * (y * mw + x) + c] = Hash(seed + 31 * m, x, y, c);
            mips.Add(d);
        }
        return Blp(3, 8, 0, w, h, null, mips);
    }

    /// <summary>Raw BGRA whose alpha is a known function of (mip, x, y) -- a height texture's, with zeros where wanted --
    /// and whose colour is the hash pattern.</summary>
    static byte[] RawAlphaBlp(int seed, int w, int h, Func<int, int, int, int> alpha)
    {
        var mips = new List<byte[]>();
        for (int m = 0; m < Levels(w, h); m++)
        {
            int mw = Math.Max(1, w >> m), mh = Math.Max(1, h >> m);
            var d = new byte[mw * mh * 4];
            for (int y = 0; y < mh; y++)
                for (int x = 0; x < mw; x++)
                {
                    for (int c = 0; c < 3; c++)
                        d[4 * (y * mw + x) + c] = Hash(seed + 31 * m, x, y, c);
                    d[4 * (y * mw + x) + 3] = (byte)alpha(m, x, y);
                }
            mips.Add(d);
        }
        return Blp(3, 8, 0, w, h, null, mips);
    }

    /// <summary>Palettized with 8-bit alpha (encoding 1).</summary>
    static byte[] PalettizedBlp(int seed, int w, int h)
    {
        var palette = new byte[1024];
        for (int i = 0; i < 1024; i++) palette[i] = Hash(seed, i, 7, 3);
        var mips = new List<byte[]>();
        for (int m = 0; m < Levels(w, h); m++)
        {
            int mw = Math.Max(1, w >> m), mh = Math.Max(1, h >> m), px = mw * mh;
            var d = new byte[2 * px];
            for (int i = 0; i < px; i++)
            {
                d[i] = Hash(seed + m, i, 1, 2);
                d[px + i] = Hash(seed + m, i, 9, 4);
            }
            mips.Add(d);
        }
        return Blp(1, 8, 0, w, h, palette, mips);
    }

    /// <summary>A DXT colour block whose endpoints keep c0 > c1 (four-colour mode) and whose indices vary.</summary>
    static void ColourBlock(byte[] d, int o, int seed, int m, int bx, int by)
    {
        ushort c0 = (ushort)(0x8000 | (Hash(seed, m, bx, by) << 7) | Hash(seed, by, m, bx));
        ushort c1 = (ushort)(Hash(seed, bx, by, m) << 6);
        if (c1 >= c0) c1 = (ushort)(c0 - 1);
        AdtSynthetic.PutU16(d, o, c0);
        AdtSynthetic.PutU16(d, o + 2, c1);
        for (int k = 0; k < 4; k++) d[o + 4 + k] = Hash(seed + 5, m * 16 + k, bx, by);
    }

    /// <summary>DXT1 (alpha 0), DXT3 (alpha 8, preferred 1) or DXT5 (alpha 8, preferred 7), every level of full blocks; DXT5's
    /// alpha blocks alternate between the eight-value mode (a0 > a1) and the six-value mode (a0 <= a1: codes 6 and 7 are
    /// 0 and 255), as real height textures do.</summary>
    static byte[] DxtBlp(int kind, int seed, int w, int h)
    {
        int block = kind == 1 ? 8 : 16;
        var mips = new List<byte[]>();
        for (int m = 0; m < Levels(w, h); m++)
        {
            int mw = Math.Max(1, w >> m), mh = Math.Max(1, h >> m);
            int bw = Math.Max(1, (mw + 3) / 4), bh = Math.Max(1, (mh + 3) / 4);
            var d = new byte[bw * bh * block];
            for (int by = 0; by < bh; by++)
                for (int bx = 0; bx < bw; bx++)
                {
                    int o = (by * bw + bx) * block;
                    if (kind == 3)
                        for (int k = 0; k < 8; k++) d[o + k] = Hash(seed + 11, m, bx * 8 + k, by);
                    else if (kind == 5)
                    {
                        byte hi = (byte)(200 + Hash(seed, m, bx, 1) % 50), lo = (byte)(Hash(seed, m, by, 2) % 150);
                        bool six = ((bx + by + m) & 1) == 1;
                        d[o] = six ? lo : hi;                              // a0 > a1: eight values; a0 <= a1: six, 0 and 255
                        d[o + 1] = six ? hi : lo;
                        for (int k = 2; k < 8; k++) d[o + k] = Hash(seed + 13, m, bx * 8 + k, by);
                    }
                    ColourBlock(d, kind == 1 ? o : o + 8, seed, m, bx, by);
                }
            mips.Add(d);
        }
        return kind == 1 ? Blp(2, 0, 0, w, h, null, mips) : Blp(2, 8, kind == 3 ? 1 : 7, w, h, null, mips);
    }

    /// <summary>Prepare (as the worker does, with this GPU's format support) one texture.</summary>
    static WmvTerrainTexture PrepareTexture(uint id, byte[] bytes, string md5 = null)
    {
        var t = new WmvTerrainTexture { FileDataID = id };
        t.Prepare(bytes, md5 ?? WmvTerrainSession.Md5Hex(bytes), WmvTerrainFormatSupport.Query());
        return t;
    }

    /// <summary>A block-compressed level as the format defines it, and as this GPU's block arithmetic was measured to
    /// decode its four-colour interpolants.</summary>
    sealed class BlockDecode
    {
        public int W, H;
        /// <summary>RGBA 0..255, the format's exact decode (not BlpDecoder's integer arithmetic): 565 endpoints widened by
        /// bit replication, the interpolated colours (2 c0 + c1) / 3 and (c0 + 2 c1) / 3 unrounded, DXT1's three-colour
        /// mode (c0 <= c1) with (c0 + c1) / 2 and transparent black, DXT3's alpha nibble x 17, DXT5's alpha ramps unrounded.</summary>
        public float[] Exact;
        /// <summary>RGB(A) 0..255: as Exact, but the four-colour interpolants by the MEASURED hardware decode --
        /// HardwareInterp5 on the stored 5-bit channels, HardwareInterp6 on the widened 6-bit one.</summary>
        public float[] Hardware;
        /// <summary>The colour selector picks c0 or c1 (or the three-colour mode's transparent black): exact on any GPU.</summary>
        public bool[] ColourEndpoint;
        /// <summary>A four-colour interpolant: the hardware model or the exact ramp.</summary>
        public bool[] ColourInterpolant;
        /// <summary>The alpha is exact for the format: DXT1, DXT3, or DXT5 selecting a0, a1, 0 or 255.</summary>
        public bool[] AlphaExact;
        /// <summary>DXT5: the texel's alpha block is in the six-value mode (a0 <= a1).</summary>
        public bool[] AlphaSix;
    }

    static int Widen5(int v) { return (v << 3) | (v >> 2); }
    static int Widen6(int v) { return (v << 2) | (v >> 4); }

    /// <summary>The measured hardware decode of the four-colour interpolant nearer a: ((2 a + b) * 22) >> 3 on the stored
    /// 5-bit values (a texel dump of every DXT3 of Retail 32_50 equals it exactly on this GPU).</summary>
    static int HardwareInterp5(int a, int b) { return ((2 * a + b) * 22) >> 3; }

    /// <summary>The measured hardware decode of the 6-bit interpolant nearer a, on the widened 8-bit values a and b:
    /// (256 a + trunc((b - a) / 4) + 128 + 80 (b - a)) >> 8 (never negative before the shift).</summary>
    static int HardwareInterp6(int a, int b)
    {
        int d = b - a;
        return (256 * a + d / 4 + 128 + 80 * d) >> 8;
    }

    static BlockDecode DecodeBlocks(byte[] file, int mip)
    {
        BlpHeader hd = BlpDecoder.ReadHeader(file);
        BlpMipLevel lv = hd.Mips[mip];
        int w = lv.Width, h = lv.Height;
        var r = new BlockDecode
        {
            W = w, H = h, Exact = new float[w * h * 4], Hardware = new float[w * h * 4], ColourEndpoint = new bool[w * h],
            ColourInterpolant = new bool[w * h], AlphaExact = new bool[w * h], AlphaSix = new bool[w * h],
        };
        int bw = Math.Max(1, (w + 3) / 4), bh = Math.Max(1, (h + 3) / 4), block = hd.BlockBytes;
        var col = new float[4, 4];
        var hw = new float[4, 3];
        var alpha = new float[8];
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++)
            {
                int o = lv.Offset + (by * bw + bx) * block;
                int co = hd.Format == BlpPixelFormat.Dxt1 ? o : o + 8;
                int c0 = file[co] | (file[co + 1] << 8), c1 = file[co + 2] | (file[co + 3] << 8);
                uint bits = (uint)(file[co + 4] | (file[co + 5] << 8) | (file[co + 6] << 16) | (file[co + 7] << 24));
                int[] r5 = { (c0 >> 11) & 31, (c1 >> 11) & 31 }, g6 = { (c0 >> 5) & 63, (c1 >> 5) & 63 }, b5 = { c0 & 31, c1 & 31 };
                for (int e = 0; e < 2; e++)
                {
                    col[e, 0] = Widen5(r5[e]);
                    col[e, 1] = Widen6(g6[e]);
                    col[e, 2] = Widen5(b5[e]);
                    col[e, 3] = 255f;
                }
                bool three = hd.Format == BlpPixelFormat.Dxt1 && c0 <= c1;
                for (int k = 0; k < 3; k++)
                {
                    col[2, k] = three ? (col[0, k] + col[1, k]) / 2f : (2f * col[0, k] + col[1, k]) / 3f;
                    col[3, k] = three ? 0f : (col[0, k] + 2f * col[1, k]) / 3f;
                }
                col[2, 3] = 255f;
                col[3, 3] = three ? 0f : 255f;
                hw[2, 0] = HardwareInterp5(r5[0], r5[1]);
                hw[2, 1] = HardwareInterp6(Widen6(g6[0]), Widen6(g6[1]));
                hw[2, 2] = HardwareInterp5(b5[0], b5[1]);
                hw[3, 0] = HardwareInterp5(r5[1], r5[0]);
                hw[3, 1] = HardwareInterp6(Widen6(g6[1]), Widen6(g6[0]));
                hw[3, 2] = HardwareInterp5(b5[1], b5[0]);
                ulong abits = 0;
                bool six = hd.Format == BlpPixelFormat.Dxt5 && file[o] <= file[o + 1];
                if (hd.Format == BlpPixelFormat.Dxt5)
                {
                    float a0 = file[o], a1 = file[o + 1];
                    alpha[0] = a0;
                    alpha[1] = a1;
                    if (a0 > a1)
                        for (int i = 1; i <= 6; i++) alpha[1 + i] = ((7 - i) * a0 + i * a1) / 7f;
                    else
                    {
                        for (int i = 1; i <= 4; i++) alpha[1 + i] = ((5 - i) * a0 + i * a1) / 5f;
                        alpha[6] = 0f;
                        alpha[7] = 255f;
                    }
                    for (int k = 0; k < 6; k++) abits |= (ulong)file[o + 2 + k] << (8 * k);
                }
                for (int t = 0; t < 16; t++)
                {
                    int x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);
                    if (x >= w || y >= h) continue;
                    int sel = (int)((bits >> (2 * t)) & 3);
                    int i = y * w + x, p = 4 * i;
                    bool interp = !three && sel >= 2;
                    for (int k = 0; k < 3; k++)
                    {
                        r.Exact[p + k] = col[sel, k];
                        r.Hardware[p + k] = interp ? hw[sel, k] : col[sel, k];
                    }
                    bool exactAlpha = true;
                    float a;
                    if (hd.Format == BlpPixelFormat.Dxt3)
                        a = ((file[o + (t >> 1)] >> (4 * (t & 1))) & 15) * 17;
                    else if (hd.Format == BlpPixelFormat.Dxt5)
                    {
                        int asel = (int)((abits >> (3 * t)) & 7);
                        a = alpha[asel];
                        exactAlpha = asel < 2 || a == 0f || a == 255f;
                    }
                    else
                        a = col[sel, 3];
                    r.Exact[p + 3] = r.Hardware[p + 3] = a;
                    r.ColourEndpoint[i] = sel < 2 || (three && sel == 3);
                    r.ColourInterpolant[i] = interp;
                    r.AlphaExact[i] = exactAlpha;
                    r.AlphaSix[i] = six;
                }
            }
        return r;
    }

    static float Diff(float a, float b)
    {
        float d = Math.Abs(a - b);
        return float.IsNaN(d) ? float.PositiveInfinity : d;
    }

    /// <summary>The largest |GPU - decode| (in 1/255) over R, G, B and over A, for one mip.</summary>
    static void TexelError(float[] gpu, byte[] rgba, int count, out float rgb, out float a)
    {
        rgb = 0f;
        a = 0f;
        for (int i = 0; i < count; i++)
            for (int c = 0; c < 4; c++)
            {
                float d = Diff(gpu[4 * i + c] * 255f, rgba[4 * i + c]);
                if (c < 3) rgb = Math.Max(rgb, d);
                else a = Math.Max(a, d);
            }
    }

    /// <summary>
    /// The RGBA32 fallback of a block-compressed file (BlpDecoder's exact block mode, uploaded as R8G8B8A8_UNorm and read
    /// back by Load) against the file's OWN block decode, computed here independently (DecodeBlocks): an endpoint colour
    /// and an alpha the format makes exact must equal the exact decode; every other value -- a colour interpolant, DXT1's
    /// three-colour midpoint, a DXT5 alpha interpolant -- the exact value rounded to nearest (Math.Round, ties to even:
    /// only the midpoint can tie). The largest |difference| (1/255); how many values of each kind were compared.
    /// </summary>
    static float FallbackError(float[] gpu, BlockDecode bd, out int exact, out int rounded)
    {
        float worst = 0f;
        exact = rounded = 0;
        for (int i = 0; i < bd.W * bd.H; i++)
            for (int k = 0; k < 4; k++)
            {
                bool isExact = k < 3 ? bd.ColourEndpoint[i] : bd.AlphaExact[i];
                float e = bd.Exact[4 * i + k];
                float want = isExact ? e : (float)Math.Round(e);
                if (isExact) exact++;
                else rounded++;
                worst = Math.Max(worst, Diff(gpu[4 * i + k] * 255f, want));
            }
        return worst;
    }

    // ================================================================ textures

    static void TerrainTextureUploadTests(Action<string> log)
    {
        int textures0 = Resources.FindObjectsOfTypeAll<Texture2D>().Length;
        var made = new List<Texture2D>();
        WmvTerrainFormatSupport support = WmvTerrainFormatSupport.Query();
        try
        {
            var cases = new[]
            {
                new { Name = "raw BGRA 16x8", Bytes = RawBlp(1, 16, 8), Exact = true, Want = GraphicsFormat.R8G8B8A8_UNorm },
                new { Name = "palettized 16x16", Bytes = PalettizedBlp(2, 16, 16), Exact = true, Want = GraphicsFormat.R8G8B8A8_UNorm },
                new { Name = "DXT1 16x16", Bytes = DxtBlp(1, 3, 16, 16), Exact = false,
                      Want = support.Dxt1 ? GraphicsFormat.RGBA_DXT1_UNorm : GraphicsFormat.R8G8B8A8_UNorm },
                new { Name = "DXT3 16x16", Bytes = DxtBlp(3, 4, 16, 16), Exact = false,
                      Want = support.Dxt3 ? GraphicsFormat.RGBA_DXT3_UNorm : GraphicsFormat.R8G8B8A8_UNorm },
                new { Name = "DXT5 32x32", Bytes = DxtBlp(5, 5, 32, 32), Exact = false,
                      Want = support.Dxt5 ? GraphicsFormat.RGBA_DXT5_UNorm : GraphicsFormat.R8G8B8A8_UNorm },
            };
            uint id = 990001;
            foreach (var c in cases)
            {
                WmvTerrainTexture t = PrepareTexture(id++, c.Bytes);
                BlpHeader h = BlpDecoder.ReadHeader(c.Bytes);
                bool prepared = t.Available && t.Ready && t.Width == h.Width && t.Height == h.Height && t.Mips == h.MipCount &&
                                t.Mips == Levels(h.Width, h.Height) && t.GraphicsFormat == c.Want && t.Bytes == c.Bytes.Length &&
                                t.Md5 == WmvTerrainSession.Md5Hex(c.Bytes);
                long gpuWant = 0;
                foreach (BlpMipLevel lv in h.Mips)
                    gpuWant += c.Want == GraphicsFormat.R8G8B8A8_UNorm ? (long)lv.Width * lv.Height * 4 : lv.DataSize;
                Texture2D tex = t.Upload(x => made.Add(x));
                bool settings = tex.graphicsFormat == c.Want && tex.mipmapCount == h.MipCount && tex.wrapMode == TextureWrapMode.Repeat &&
                                tex.filterMode == FilterMode.Trilinear && tex.anisoLevel == WmvTerrainMaterials.LayerAnisoLevel &&
                                tex.mipMapBias == 0f && !tex.isReadable && !t.Ready && t.UploadMs >= 0 && t.GpuBytes == gpuWant;
                float worstRgb = 0f, worstA = 0f, worstVsDecoder = 0f;
                float worstEnd = 0f, worstAlphaExact = 0f, worstAlphaInterp = 0f, worstAlphaInterpSix = 0f, worstRamp = 0f, worstHardware = 0f;
                int endpoints = 0, interpolated = 0, rampMatches = 0, hardwareMatches = 0, unmodelled = 0;
                int alphaExact = 0, alphaExactSix = 0, alphaInterp = 0, alphaInterpSix = 0, alphaRounded = 0;
                var perMip = new List<string>();
                bool fallback = !c.Exact && tex.graphicsFormat == GraphicsFormat.R8G8B8A8_UNorm;
                float worstFallback = 0f;
                for (int m = 0; m < h.MipCount; m++)
                {
                    int w, hh;
                    float[] gpu = WmvTerrainComposite.ReadTexels(tex, m, out w, out hh);
                    BlpImage decoded = BlpDecoder.Decode(c.Bytes, m);
                    float rgb, a;
                    TexelError(gpu, decoded.Rgba, w * hh, out rgb, out a);
                    worstVsDecoder = Math.Max(worstVsDecoder, Math.Max(rgb, a));
                    if (c.Exact)
                    {
                        worstRgb = Math.Max(worstRgb, rgb);
                        worstA = Math.Max(worstA, a);
                        perMip.Add(rgb.ToString("0.###") + "/" + a.ToString("0.###"));
                        continue;
                    }
                    if (fallback)
                    {
                        // This GPU took the file as RGBA32 (it does not sample the format): the file's own block decode, not BlpDecoder.
                        int ne, nr;
                        float e = FallbackError(gpu, DecodeBlocks(c.Bytes, m), out ne, out nr);
                        worstFallback = Math.Max(worstFallback, e);
                        perMip.Add(e.ToString("0.###"));
                        continue;
                    }
                    // Block-compressed: the stored blocks, texel by texel, colour and alpha apart.
                    BlockDecode bd = DecodeBlocks(c.Bytes, m);
                    float mipEnd = 0f, mipHardware = 0f;
                    for (int i = 0; i < bd.W * bd.H; i++)
                    {
                        float ga = gpu[4 * i + 3] * 255f, ea = bd.Exact[4 * i + 3];
                        if (bd.AlphaExact[i])
                        {
                            alphaExact++;
                            if (bd.AlphaSix[i]) alphaExactSix++;
                            worstAlphaExact = Math.Max(worstAlphaExact, Diff(ga, ea));
                        }
                        else
                        {
                            alphaInterp++;
                            if (bd.AlphaSix[i])
                            {
                                alphaInterpSix++;
                                worstAlphaInterpSix = Math.Max(worstAlphaInterpSix, Diff(ga, ea));
                            }
                            else
                                worstAlphaInterp = Math.Max(worstAlphaInterp, Diff(ga, ea));
                            if (Diff(ga, (float)Math.Round(ea, MidpointRounding.AwayFromZero)) <= TexelExact) alphaRounded++;
                        }
                        if (bd.ColourEndpoint[i])
                        {
                            endpoints++;
                            for (int k = 0; k < 3; k++)
                                mipEnd = Math.Max(mipEnd, Diff(gpu[4 * i + k] * 255f, bd.Exact[4 * i + k]));
                        }
                        else if (bd.ColourInterpolant[i])
                        {
                            interpolated++;
                            float dr = 0f, dh = 0f;
                            for (int k = 0; k < 3; k++)
                            {
                                dr = Math.Max(dr, Diff(gpu[4 * i + k] * 255f, bd.Exact[4 * i + k]));
                                dh = Math.Max(dh, Diff(gpu[4 * i + k] * 255f, bd.Hardware[4 * i + k]));
                            }
                            if (dr <= TexelExact) rampMatches++;
                            if (dh <= TexelExact) hardwareMatches++;
                            worstRamp = Math.Max(worstRamp, dr);
                            mipHardware = Math.Max(mipHardware, dh);
                        }
                        else
                            unmodelled++;
                    }
                    worstEnd = Math.Max(worstEnd, mipEnd);
                    worstHardware = Math.Max(worstHardware, mipHardware);
                    perMip.Add(mipEnd.ToString("0.###") + "/" + mipHardware.ToString("0.###"));
                }
                bool texels;
                string how;
                if (c.Exact)
                {
                    // Raw and palettized go up decoded by BlpDecoder: the GPU must give those bytes back exactly.
                    texels = worstRgb <= TexelExact && worstA <= TexelExact;
                    how = "every stored mip read back by Load exactly as BlpDecoder decodes it (R,G,B / A max per mip, 1/255: " +
                          string.Join(" ", perMip.ToArray()) + ")";
                }
                else if (fallback)
                {
                    texels = worstFallback <= TexelExact && t.Detail.EndsWith(WmvTerrainTexture.ExactBlocksNote, StringComparison.Ordinal);
                    how = "decoded to RGBA32 (\"" + t.Detail + "\"): every stored mip equal to the file's own block decode -- endpoints and " +
                          "exact alphas exact, interpolants the exact value rounded (max per mip, 1/255: " + string.Join(" ", perMip.ToArray()) +
                          "); BlpDecoder's default (truncating) mode differs by up to " + worstVsDecoder.ToString("0.##") + "/255";
                }
                else
                {
                    // Endpoint colours and the format's exact alphas exactly; every interpolated colour exactly one named decode.
                    bool hardware = interpolated > 0 && hardwareMatches == interpolated;
                    bool ramp = interpolated > 0 && rampMatches == interpolated;
                    texels = endpoints > 0 && interpolated > 0 && worstEnd <= TexelExact && worstAlphaExact <= TexelExact && unmodelled == 0 &&
                             (hardware || ramp) && alphaExact > 0 && (h.Format != BlpPixelFormat.Dxt5 || (alphaExactSix > 0 && alphaInterpSix > 0));
                    how = string.Format("{0} endpoint colours exact (max {1:0.###}/255); {2} alphas exact for the format (max {3:0.###}/255){4}; " +
                                        "{5} interpolated colours equal to {6} (the measured hardware model: {7} of them, max {8:0.###}/255; the " +
                                        "exact ramp: {9}, max {10:0.###}/255){11}; endpoint / hardware-model max per mip, 1/255: {12}; BlpDecoder " +
                                        "(truncating) differs by up to {13:0.##}/255",
                                        endpoints, worstEnd, alphaExact, worstAlphaExact,
                                        alphaInterp > 0 ? string.Format(" ({0} of them in six-value blocks, 0 and 255 among them); {1} interpolated DXT5 " +
                                                                        "alphas measured: max {2:0.###}/255 off the exact ramp in eight-value blocks, " +
                                                                        "{3:0.###}/255 in the {4} of six-value blocks, {5} of them the ramp rounded (no limit)",
                                                                        alphaExactSix, alphaInterp, worstAlphaInterp, worstAlphaInterpSix, alphaInterpSix,
                                                                        alphaRounded) : "",
                                        interpolated, hardware ? "THE HARDWARE MODEL" : ramp ? "THE EXACT RAMP" : "NEITHER model",
                                        hardwareMatches, worstHardware, rampMatches, worstRamp,
                                        unmodelled > 0 ? "; " + unmodelled + " three-colour midpoints (no measured model)" : "",
                                        string.Join(" ", perMip.ToArray()), worstVsDecoder);
                }
                Check(prepared && settings && texels,
                      string.Format("terrain textures: {0} -- prepared ({1}, {2}x{3}, {4} stored mip(s), {5} GPU bytes), uploaded as {6} with its own " +
                                    "{7} mips, Repeat, trilinear, aniso {8}, mip bias {10}, no CPU copy; {9}", c.Name, t.Encoding, t.Width, t.Height, t.Mips,
                                    t.GpuBytes, tex.graphicsFormat, tex.mipmapCount, tex.anisoLevel, how, tex.mipMapBias), log);
            }

            // Asymmetry and rows as stored: texel (1, 0) and (0, 1) differ in the file and on the GPU as in the file.
            byte[] asym = RawBlp(77, 8, 8, 1);
            Texture2D at = PrepareTexture(990010, asym).Upload(x => made.Add(x));
            int aw, ah;
            float[] ag = WmvTerrainComposite.ReadTexels(at, 0, out aw, out ah);
            BlpImage ad = BlpDecoder.Decode(asym, 0);
            bool rowsAsStored = Math.Abs(ag[4 * 1] * 255f - ad.Rgba[4 * 1]) < TexelExact && Math.Abs(ag[4 * 8] * 255f - ad.Rgba[4 * 8]) < TexelExact &&
                                ad.Rgba[4 * 1] != ad.Rgba[4 * 8] && Math.Abs(ag[4 * 63] * 255f - ad.Rgba[4 * 63]) < TexelExact;
            Check(rowsAsStored, "terrain textures: rows go up as stored (no flip): stored texel (1, 0) " + ad.Rgba[4] + " and (0, 1) " + ad.Rgba[32] +
                  " read back " + (ag[4] * 255f).ToString("0.#") + " and " + (ag[32] * 255f).ToString("0.#"), log);

            // Failures: the player's own states.
            byte[] good = RawBlp(8, 8, 8);
            WmvTerrainTexture md5 = PrepareTexture(990011, good, "00000000000000000000000000000000");
            WmvTerrainTexture junk = PrepareTexture(990012, new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 });
            byte[] bad = RawBlp(9, 8, 8);
            bad[0] = (byte)'X';
            WmvTerrainTexture magic = PrepareTexture(990013, bad);
            string junkWhy = ThrownMessage(() => BlpDecoder.ReadHeader(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }));
            Check(md5.State == WmvTerrainTexture.StateMd5Mismatch && md5.Detail == "the bytes received hash to " + WmvTerrainSession.Md5Hex(good) +
                  ", the host served 00000000000000000000000000000000" && !md5.Ready &&
                  junk.State == WmvTerrainTexture.StateDecodeFailed && junk.Detail == junkWhy && !junk.Ready &&
                  magic.State == WmvTerrainTexture.StateDecodeFailed && magic.Detail.Contains("BLP2"),
                  "terrain textures: md5 mismatch (\"" + md5.Detail + "\"), decode failed (\"" + junk.Detail + "\"; \"" + magic.Detail +
                  "\") -- nothing to upload", log);
            // A block-compressed file that is not whole 4x4 blocks goes up decoded (BlpDecoder's exact block mode), and says so:
            // every stored mip read back equals the file's own independent block decode (DecodeBlocks), not BlpDecoder's.
            byte[] odd = DxtBlp(5, 6, 6, 6);
            WmvTerrainTexture oddT = PrepareTexture(990014, odd);
            Texture2D oddTex = oddT.Available ? oddT.Upload(x => made.Add(x)) : null;
            float oddWorst = float.PositiveInfinity, oddVsDefault = 0f;
            int oddExact = 0, oddRounded = 0;
            var oddPerMip = new List<string>();
            if (oddTex != null)
            {
                oddWorst = 0f;
                for (int m = 0; m < oddT.Mips; m++)
                {
                    int w, hh, ne, nr;
                    float[] gpu = WmvTerrainComposite.ReadTexels(oddTex, m, out w, out hh);
                    float e = FallbackError(gpu, DecodeBlocks(odd, m), out ne, out nr);
                    oddWorst = Math.Max(oddWorst, e);
                    oddExact += ne;
                    oddRounded += nr;
                    oddPerMip.Add(w + "x" + hh + " " + e.ToString("0.###"));
                    float rgb, a;
                    TexelError(gpu, BlpDecoder.Decode(odd, m).Rgba, w * hh, out rgb, out a);
                    oddVsDefault = Math.Max(oddVsDefault, Math.Max(rgb, a));
                }
            }
            Check(oddT.Available && oddT.GraphicsFormat == GraphicsFormat.R8G8B8A8_UNorm && oddT.Detail.Contains("not whole 4x4 blocks") &&
                  oddT.Detail.EndsWith(WmvTerrainTexture.ExactBlocksNote, StringComparison.Ordinal) && oddTex != null &&
                  oddTex.mipmapCount == oddT.Mips && oddWorst <= TexelExact && oddExact > 0 && oddRounded > 0,
                  string.Format("terrain textures: a 6x6 DXT5 is decoded per stored mip to RGBA32 instead (\"{0}\"); read back, every stored mip " +
                                "equals the file's own block decode -- {1} endpoint and exact-alpha values exact, {2} interpolants the exact " +
                                "value rounded to nearest (max per mip, 1/255: {3}); BlpDecoder's default (truncating) mode differs by up to " +
                                "{4:0.##}/255", oddT.Detail, oddExact, oddRounded, string.Join(", ", oddPerMip.ToArray()), oddVsDefault), log);
        }
        finally
        {
            foreach (Texture2D t in made) if (t != null) UnityEngine.Object.DestroyImmediate(t);
        }
        Check(Resources.FindObjectsOfTypeAll<Texture2D>().Length == textures0,
              "terrain textures: every texture these checks uploaded is gone (" + Resources.FindObjectsOfTypeAll<Texture2D>().Length + " vs " +
              textures0 + ")", log);
    }

    // ================================================================ the synthetic textured tiles

    // MDID entries (index: texture, size, MTXP scale -> repeats; MHID height texture; MTXP height scale, offset):
    //    0 T1 16x16 (scale 0: 8 repeats)          6 T6 8x8 (8); height H6 992006, a DXT5 16x16; (5, 1)
    //    1 T2 8x8 (scale 1: 4)                    7 T7 8x8 (8): the static layer effect (MCLY 0x40) where used
    //    2 T3 32x32 (scale 3: 1)                  8 T8 16x16 (8); height H8 992008, raw 8x8, alpha 0 in columns 0..2; (1, 0)
    //    3 T4 4x4 (scale 0: 8)                    9 T9 8x8 (scale 1: 4); height H9 992009, raw 8x8, zeros too; (1, 0)
    //    4 T5 16x16 (scale 2: 2)                 10 T10 8x8 (8): the cube-map layer (MCLY 0x400) where used
    //    5 MDID 0 (no texture id)                11 T11 8x8 (8); height 992011, which no host has; (4, 0.5)
    // MHID 0 and MTXP heights (0, 1) elsewhere. With MPHD 0x80 (MatHeightsOn) the heights enter the mix; with MPHD 0x4
    // (MatHeightsOff, 8-bit alpha too) MHID and the MTXP heights are ignored (H = 1).
    const uint MatHeightsOn = 0x80, MatHeightsOff = 0x04;
    static readonly uint[] MatMdid = { 991001, 991002, 991003, 991004, 991005, 0, 991006, 991007, 991008, 991009, 991010, 991011 };
    static readonly uint[] MatMhid = { 0, 0, 0, 0, 0, 0, 992006, 0, 992008, 992009, 0, 992011 };
    static readonly int[] MatScale = { 0, 1, 3, 0, 2, 0, 0, 0, 0, 1, 0, 0 };
    static readonly int[] MatSize = { 16, 8, 32, 4, 16, 8, 8, 8, 16, 8, 8, 8 };
    static readonly float[] MatHeightScale = { 0f, 0f, 0f, 0f, 0f, 0f, 5f, 0f, 1f, 1f, 0f, 4f };
    static readonly float[] MatHeightOffset = { 1f, 1f, 1f, 1f, 1f, 1f, 1f, 1f, 0f, 0f, 1f, 0.5f };
    /// <summary>The height texture no host serves (its chunk 15 is HeightTextureMissing with MPHD 0x80).</summary>
    const uint MatMissingHeight = 992011;
    static readonly uint[] MatHeightTextures = { 992006, 992008, 992009 };

    static Dictionary<uint, byte[]> MaterialTestTextures()
    {
        var d = new Dictionary<uint, byte[]>();
        for (int i = 0; i < MatMdid.Length; i++)
            if (MatMdid[i] != 0)
                d[MatMdid[i]] = RawBlp(100 + i, MatSize[i], MatSize[i]);
        d[992006] = DxtBlp(5, 66, 16, 16);
        // H8: alpha 0 in columns 0..2 of every repeat; H9: alpha 0 in rows 0..3 of its columns 0..5 -- where both are 0 the
        // height mix has S = 0 and falls back to the raw weights.
        d[992008] = RawAlphaBlp(68, 8, 8, (m, x, y) => m == 0 ? (x < 3 ? 0 : 30 + 28 * (x - 3) + 3 * y) : Hash(68, m, x, y));
        d[992009] = RawAlphaBlp(69, 8, 8, (m, x, y) => m == 0 ? (x < 6 ? (y < 4 ? 0 : 20 * y) : 100 + 10 * y) : Hash(69, m, x, y));
        return d;
    }

    static byte[] AlphaOf(Func<int, int, int> f) { return AdtSynthetic.AlphaMapOf(f); }

    /// <summary>MCCV bytes (B, G, R, A per vertex) from a function of the vertex.</summary>
    static byte[] Mccv(Func<int, int, int> f)
    {
        var b = new byte[580];
        for (int k = 0; k < 145; k++)
            for (int c = 0; c < 3; c++)
                b[4 * k + c] = (byte)f(k, c);
        return b;
    }

    /// <summary>
    /// The synthetic tile's tex0 chunks (states with MPHD 0x80 / without):
    ///    0 one layer                                  10 T1 + T6 (height (5, 1)): the heights decide
    ///    1 two layers, RLE                            11 T1 + T7 with MCLY 0x40: LayerEffectStatic, drawn static
    ///    2 three, raw and RLE, MCCV above 1           12 no layer: NoLayers
    ///    3 four layers                                13 three layers at 32 texels per chunk (production sampling)
    ///    4 five layers                                14 T8 + T9 (heights h, offset 0): S = 0 where both are 0
    ///    5 a layer naming MDID 0: MissingTexture      15 T1 + T11 (its height never served): HeightTextureMissing / Textured
    ///    6 holes                                      16 T10 with MCLY 0x400 + T2: LayerEffectUnresolved (T10 plain-only)
    ///    7 alpha sum above 255: Malformed             17 T6 + T8 + T1 with MCCV: three heights, one of them 1
    ///    8 MCNK 0x8000 clear: AlphaUnsupported        18 T1, T2, T9 (height (1, 0)), T6 (height (5, 1)), T8 (height (1, 0)),
    ///    9 six layers (*)                                MCCV: height textures at layer indices 2, 3 and 4, the heights decide
    ///                                                 19.. one layer (T4), MCCV each its own: the placement check
    /// (*) A load draws chunk 9's six layers (up to 8 are drawn). MaterialTestContent appends three layers to its PARSED
    /// tex0 -- the only way past AdtParser, which makes more than 8 MCLY entries Malformed -- so the composite, dump and
    /// view checks keep a LayerCountUnsupported chunk (nine layers); the session checks load the bytes as they are.
    /// </summary>
    static AdtSynthetic.TexChunkSpec MaterialChunk(int n)
    {
        var L = new Func<uint, AlphaStorage, byte[], AdtSynthetic.TexLayerSpec>((i, s, a) => AdtSynthetic.Layer(i, s, a));
        var rle = AlphaStorage.Compressed;
        var raw = AlphaStorage.Uncompressed8;
        switch (n)
        {
            case 0: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0));
            case 1: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(1, rle, AlphaOf((r, c) => c * 4)));
            case 2:
                return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(2), L(0, raw, AlphaOf((r, c) => (c * 3 + r) % 120)),
                                                    L(1, rle, AlphaOf((r, c) => r < 40 ? (c * 2 + r) % 126 : 0)));
            case 3:
                return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(1), L(4, raw, AlphaOf((r, c) => (r * 5) % 80)),
                                                    L(3, rle, AlphaOf((r, c) => c < 32 ? 70 : (r + c) % 60)), L(2, raw, AlphaOf((r, c) => (r * c) % 90)));
            case 4:
                return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(1, rle, AlphaOf((r, c) => (r + 2 * c) % 63)),
                                                    L(2, raw, AlphaOf((r, c) => (3 * r + c) % 63)), L(3, rle, AlphaOf((r, c) => r > 30 ? 60 : 3)),
                                                    L(4, raw, AlphaOf((r, c) => (r * c + 7) % 63)));
            case 5: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(5, rle, AlphaOf((r, c) => 4 * (63 - c))));
            case 6: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(3), L(1, rle, AlphaOf((r, c) => 4 * r)));
            case 7:
                return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(1, raw, AlphaOf((r, c) => r == 5 && c == 9 ? 200 : 10)),
                                                    L(2, raw, AlphaOf((r, c) => r == 5 && c == 9 ? 100 : 10)));
            case 8: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(1, rle, AlphaOf((r, c) => c)));
            case 9:
                return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(1, rle, AlphaOf((r, c) => 10)), L(2, rle, AlphaOf((r, c) => 10)),
                                                    L(3, rle, AlphaOf((r, c) => 10)), L(4, rle, AlphaOf((r, c) => 10)), L(1, rle, AlphaOf((r, c) => 10)));
            case 10: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(6, rle, AlphaOf((r, c) => 2 * c)));
            case 11:
                {
                    var effect = AdtSynthetic.Layer(7, rle, AlphaOf((r, c) => 3 * r));
                    effect.ExtraFlags = 0x40;
                    return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), effect);
                }
            case 12: return AdtSynthetic.TexChunkSpec.Of();
            case 13:
                return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(3), L(1, rle, AlphaOf((r, c) => (r * 3 + c) % 100)),
                                                    L(4, raw, AlphaOf((r, c) => (c * 5) % 120)));
            case 14: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(8), L(9, rle, AlphaOf((r, c) => (r * 4 + c) % 200)));
            case 15: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(11, raw, AlphaOf((r, c) => 3 * c)));
            case 16:
                {
                    var cube = AdtSynthetic.Layer(10);
                    cube.ExtraFlags = 0x400;
                    return AdtSynthetic.TexChunkSpec.Of(cube, L(1, rle, AlphaOf((r, c) => 2 * r)));
                }
            case 17:
                return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(6), L(8, raw, AlphaOf((r, c) => (r * 2) % 128)),
                                                    L(0, rle, AlphaOf((r, c) => (c * 2) % 120)));
            case 18:
                // The shader's third, fourth and fifth height paths (_WmvHeight2 with the vectors' .z, _WmvHeight3 with .w,
                // _WmvHeight4 with the 4-suffixed scalars) with real height textures and MTXP heights that are not the default;
                // alpha sums at most 246.
                return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(0), L(1, rle, AlphaOf((r, c) => (r * 2) % 50)),
                                                    L(9, raw, AlphaOf((r, c) => (c * 3) % 50)),
                                                    L(6, rle, AlphaOf((r, c) => 20 + (r + 2 * c) % 60)),
                                                    L(8, raw, AlphaOf((r, c) => 10 + (r * c) % 60)));
            default: return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(3));
        }
    }

    /// <summary>Every chunk's state on MaterialTestContent's synthetic tile (chunk 9 with its three appended layers), with MPHD
    /// 0x80 (on) or without (off); 18 and up are Textured.</summary>
    static TerrainMaterialState MaterialTestState(int n, bool heightsOn)
    {
        switch (n)
        {
            case 5: return TerrainMaterialState.MissingTexture;
            case 7: return TerrainMaterialState.Malformed;
            case 8: return TerrainMaterialState.AlphaUnsupported;
            case 9: return TerrainMaterialState.LayerCountUnsupported;
            case 11: return TerrainMaterialState.LayerEffectStatic;
            case 12: return TerrainMaterialState.NoLayers;
            case 15: return heightsOn ? TerrainMaterialState.HeightTextureMissing : TerrainMaterialState.Textured;
            case 16: return TerrainMaterialState.LayerEffectUnresolved;
            default: return TerrainMaterialState.Textured;
        }
    }

    /// <summary>The synthetic tex0 (MHID `mhid`, MatMhid when not given).</summary>
    static byte[] MaterialTestTex0(uint[] mhid = null)
    {
        var mtxp = new List<byte>();
        for (int i = 0; i < MatMdid.Length; i++)
            mtxp.AddRange(AdtSynthetic.MtxpEntry((uint)(MatScale[i] << 4), MatHeightScale[i], MatHeightOffset[i]));
        return AdtSynthetic.Tex0(new AdtSynthetic.Tex0Spec { Mdid = MatMdid, Mhid = mhid ?? MatMhid, Mtxp = mtxp.ToArray(), Chunk = MaterialChunk });
    }

    static byte[] MaterialTestRoot(int tx, int ty)
    {
        return AdtSynthetic.RootWith(tx, ty, n => new RootChunkSpec
        {
            Flags = n == 8 ? 0x10000u : 0x18000u,                       // 0x8000 everywhere but chunk 8 (AlphaUnsupported)
            BaseHeight = 50f,
            Height = k => (k % 17) * 0.05f,
            Holes = n == 6 ? (1UL << 0) | (1UL << 9) | (1UL << 27) | (1UL << 63) : 0UL,
            VertexColorBytes = n == 2 ? Mccv((k, c) => c == 2 ? 120 + (k * 7) % 134 : c == 1 ? 127 + (k % 9) * 10 : 60 + (k * 13) % 90)
                             : n == 17 || n >= 18 ? Mccv((k, c) => 40 + (Hash(n, c, 1, 2) % 200) - (k % 5)) : null,
        });
    }

    /// <summary>The synthetic textured tile with MPHD `mphd`: root, tex0 (chunk 9 with three layers appended to the parsed
    /// tex0: nine, LayerCountUnsupported -- see MaterialChunk), the material, every texture prepared (the height texture no
    /// host has failed as a host would answer it) and the failures settled as the session settles them; the provenance
    /// (build, md5s of the bytes) a load would carry.</summary>
    static WmvTerrainTileContent MaterialTestContent(int tx, int ty, uint mphd, out TerrainTileMesh mesh, out Dictionary<uint, byte[]> blps)
    {
        byte[] rootBytes = MaterialTestRoot(tx, ty);
        byte[] tex0 = MaterialTestTex0();
        AdtRoot root = AdtParser.ParseRoot(rootBytes, tx, ty);
        mesh = TerrainMeshBuilder.Build(root, tx, ty, WorldToUnity.TileOrigin(tx, ty));
        AdtTex parsed = AdtParser.ParseTex(tex0);
        for (uint k = 2; k <= 4; k++)
            AppendParsedLayer(parsed.Mcnk[9], k, AlphaOf((r, c) => 10));
        TerrainTileMaterial mat = TerrainMaterialBuilder.Build(root, parsed, mphd);
        blps = MaterialTestTextures();
        var content = new WmvTerrainTileContent
        {
            Material = mat, Tex0FileDataID = MatTex0, MapTextureFileDataID = MatMapTexture, MphdFlags = mphd, Build = "0.0.4.1",
            WdtMd5 = WmvTerrainSession.Md5Hex(TexturedWdt(tx, ty, mphd, MatRoot, MatTex0, MatMapTexture)),
            RootMd5 = WmvTerrainSession.Md5Hex(rootBytes), Tex0Md5 = WmvTerrainSession.Md5Hex(tex0),
        };
        foreach (KeyValuePair<uint, byte[]> kv in blps)
            content.Textures[kv.Key] = PrepareTexture(kv.Key, kv.Value);
        var none = new WmvTerrainTexture { FileDataID = MatMissingHeight };
        none.Fail("not in client storage", "test: no such file");
        content.Textures[MatMissingHeight] = none;
        List<uint> failedDiffuse, failedHeight;
        content.SettleFailures(out failedDiffuse, out failedHeight);
        return content;
    }

    /// <summary>Append one RLE layer naming MDID entry `textureIndex` with `alpha` to a PARSED tex0 chunk (AdtParser never
    /// gives more than 8 layers).</summary>
    static void AppendParsedLayer(AdtTexChunk tc, uint textureIndex, byte[] alpha)
    {
        var layers = new List<Wmv.Wow.World.TerrainLayer>(tc.Layers ?? new Wmv.Wow.World.TerrainLayer[0]);
        layers.Add(new Wmv.Wow.World.TerrainLayer { TextureIndex = textureIndex, Flags = 0x300 });
        var maps = new List<AlphaMap>(tc.Alpha ?? new AlphaMap[0]);
        while (maps.Count < layers.Count - 1) maps.Add(null);
        maps.Add(new AlphaMap { Storage = AlphaStorage.Compressed, Samples = (byte[])alpha.Clone(), StoredBytes = 0 });
        tc.Layers = layers.ToArray();
        tc.Alpha = maps.ToArray();
    }

    /// <summary>The CPU twin's texels: every texture the tile uploaded, mip 0 as THIS GPU decodes it (the texel dump's path),
    /// as V1 composes its reference -- so the comparison is of the composite, not of the block decoder.</summary>
    static Func<uint, TerrainTexels> GpuTexels(WmvRuntimeTerrainTile rt)
    {
        var cache = new Dictionary<uint, TerrainTexels>();
        return id =>
        {
            TerrainTexels t;
            if (cache.TryGetValue(id, out t)) return t;
            Texture2D tex;
            if (!rt.Textures.TryGetValue(id, out tex)) return null;
            int w, h;
            float[] px = WmvTerrainComposite.ReadTexels(tex, 0, out w, out h);
            t = TerrainTexels.FromRgba(w, h, px);
            cache[id] = t;
            return t;
        };
    }

    /// <summary>The dump of one chunk against the CPU at every pixel: the largest |difference| (1/255) over non-hole
    /// pixels, how many non-hole pixels are not covered and how many hole pixels are.</summary>
    static float ChunkError(float[] gpu, float[] cpu, int size, ulong holes, out int uncovered, out int holeDrawn)
    {
        float worst = 0f;
        uncovered = holeDrawn = 0;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
            {
                float u = (x + 0.5f) * 8f / size, v = (y + 0.5f) * 8f / size;
                int o = 4 * (y * size + x);
                if (TerrainCompositor.IsHole(holes, u, v))
                {
                    if (gpu[o + 3] != 0f) holeDrawn++;
                    continue;
                }
                if (gpu[o + 3] != 1f) uncovered++;
                for (int c = 0; c < 3; c++)
                    worst = Math.Max(worst, Diff(gpu[o + c], cpu[o + c]) * 255f);
            }
        return worst;
    }

    /// <summary>Symmetry s (0 identity, 1 mirror x, 2 mirror y, 3 rotate 180, 4 transpose, 5 anti-transpose, 6 rotate 90,
    /// 7 rotate 270) of a square float RGBA image.</summary>
    static float[] Symmetry(float[] img, int size, int s)
    {
        var o = new float[img.Length];
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
            {
                int sx = x, sy = y, m = size - 1;
                switch (s)
                {
                    case 1: sx = m - x; break;
                    case 2: sy = m - y; break;
                    case 3: sx = m - x; sy = m - y; break;
                    case 4: sx = y; sy = x; break;
                    case 5: sx = m - y; sy = m - x; break;
                    case 6: sx = y; sy = m - x; break;
                    case 7: sx = m - y; sy = x; break;
                }
                Array.Copy(img, 4 * (sy * size + sx), o, 4 * (y * size + x), 4);
            }
        return o;
    }

    /// <summary>Chunk c of `rt` rendered alone at its validation size (the drawn materials pinned to the authored composite
    /// unless keepView), or null with the reason; `size` gets the size.</summary>
    static float[] RenderChunk(WmvRuntimeTerrainTile rt, TerrainTileMesh mesh, TerrainChunkMaterial c, bool validation, bool unclamped,
                               out int size, out string why, bool keepView = false)
    {
        size = 0;
        int? s = WmvTerrainComposite.ValidationSize(c, id =>
        {
            Texture2D t;
            return rt.Textures.TryGetValue(id, out t) ? t : null;
        }, out why);
        float x0, zn, ex, ez;
        if (s == null || !WmvTerrainComposite.ChunkRegion(mesh, c.Index, out x0, out zn, out ex, out ez))
        {
            why = why ?? "the chunk's mesh gives no square";
            return null;
        }
        size = s.Value;
        Action restore;
        Action<UnityEngine.Rendering.CommandBuffer> draws = WmvTerrainComposite.ChunkDraws(rt, c.Index, out restore, keepView);
        try { return WmvTerrainComposite.RenderToArray(draws, x0, zn, ex, ez, 40f, 60f, size, size, validation, unclamped); }
        finally { restore(); }
    }

    static List<string> WrongStates(TerrainTileMaterial mat, bool heightsOn)
    {
        var wrong = new List<string>();
        for (int n = 0; n < 256; n++)
            if (mat.Chunks[n].State != MaterialTestState(n, heightsOn))
                wrong.Add(n + ": " + mat.Chunks[n].State + " (" + mat.Chunks[n].Reason + ")");
        return wrong;
    }

    static void TerrainCompositeTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        TerrainTileMesh mesh;
        Dictionary<uint, byte[]> blps;
        WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
        TerrainTileMaterial mat = content.Material;
        TerrainTileMesh meshOff;
        Dictionary<uint, byte[]> blpsOff;
        WmvTerrainTileContent contentOff = MaterialTestContent(tx, ty, MatHeightsOff, out meshOff, out blpsOff);
        TerrainTileMaterial matOff = contentOff.Material;
        List<string> wrongStates = WrongStates(mat, true), wrongOff = WrongStates(matOff, false);
        var seen = new HashSet<TerrainMaterialState>();
        foreach (TerrainChunkMaterial c in mat.Chunks) seen.Add(c.State);
        bool heightLists = mat.UseHeightTextures && string.Join(",", mat.HeightTextureIds) == "992006,992008,992009,992011" &&
                           !matOff.UseHeightTextures && matOff.HeightTextureIds.Length == 0 && matOff.Notes.Exists(s => s.Contains("ignored"));
        Check(wrongStates.Count == 0 && wrongOff.Count == 0 && seen.Count == TerrainTileMaterial.StateCount && heightLists &&
              mat.State == TerrainTileMaterialState.Partial && mesh.ChunksBuilt == 256,
              "terrain composite: (precondition) the synthetic tile has every one of the " + TerrainTileMaterial.StateCount + " states where it " +
              "was put with MPHD 0x80 (" + (wrongStates.Count == 0 ? "all as built" : string.Join("; ", wrongStates.ToArray())) + ") and the " +
              "same bytes without 0x80 have chunk 15 Textured (" + (wrongOff.Count == 0 ? "all as built" : string.Join("; ", wrongOff.ToArray())) +
              "); height textures " + string.Join(", ", mat.HeightTextureIds) + " with 0x80, none without (" +
              string.Join("; ", matOff.Notes.ToArray()) + ")", log);

        var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9001);
        try
        {
            WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = -1 }, null);
            rt.Root.SetActive(true);
            Func<uint, TerrainTexels> cpu = GpuTexels(rt);
            bool heightsUp = true;
            foreach (uint hid in MatHeightTextures) heightsUp &= rt.Textures.ContainsKey(hid);

            // ---- the height mix: every drawable chunk alone at its validation size, against the CPU twin at every pixel
            var results = new List<string>();
            var deciding = new List<string>();
            bool allWithin = true, heightsDecide = true;
            int drawn = 0;
            float[] chunk2 = null, chunk2Cpu = null;
            int size2 = 0;
            foreach (int n in new[] { 0, 1, 2, 3, 4, 5, 6, 10, 11, 13, 14, 17, 18, 20 })
            {
                TerrainChunkMaterial c = mat.Chunks[n];
                int s;
                string why;
                float[] gpu = RenderChunk(rt, mesh, c, true, true, out s, out why);
                if (gpu == null)
                {
                    allWithin = false;
                    results.Add(n + ": " + why);
                    continue;
                }
                float[] cpuImg = TerrainCompositor.ComposeGrid(c, cpu, s, false);
                int uncovered, holeDrawn, u2, h2;
                float err = ChunkError(gpu, cpuImg, s, c.HoleMask, out uncovered, out holeDrawn);
                bool ok = err <= 0.5f && uncovered == 0 && holeDrawn == 0;
                allWithin &= ok;
                drawn++;
                string plainText = "";
                if (c.Layers.Length > 1)
                {
                    float plain = ChunkError(gpu, TerrainCompositor.ComposeGrid(c, cpu, s, false, TerrainBlendRule.PlainWeights), s, c.HoleMask, out u2, out h2);
                    plainText = string.Format(", plain weights {0:0.#}", plain);
                    if (n == 10 || n == 14 || n == 17 || n == 18)
                    {
                        // The heights decide here: neither plain weights nor the same chunk without MPHD 0x80 (H = 1) fits.
                        float off = ChunkError(gpu, TerrainCompositor.ComposeGrid(matOff.Chunks[n], cpu, s, false), s, c.HoleMask, out u2, out h2);
                        bool decides = plain > 2f && off > 2f;
                        heightsDecide &= decides;
                        deciding.Add(string.Format("{0}: plain weights {1:0.#}/255, without the heights {2:0.#}/255", n, plain, off));
                    }
                }
                results.Add(string.Format("{0} ({1}, {2} layer(s), {3} px): max {4:0.###}/255{5}{6}{7}", n, c.State, c.Layers.Length, s, err, plainText,
                                          uncovered > 0 ? ", " + uncovered + " uncovered" : "", holeDrawn > 0 ? ", " + holeDrawn + " hole px drawn" : ""));
                if (n == 2)
                {
                    chunk2 = gpu;
                    chunk2Cpu = cpuImg;
                    size2 = s;
                }
            }
            Check(allWithin && drawn == 14 && heightsUp,
                  "terrain composite: the HEIGHT MIX -- every drawable chunk (Textured, MissingTexture, LayerEffectStatic) rendered alone at the " +
                  "design's size (validation sampling, float target, unclamped) equals TerrainCompositor within 0.5/255 at every non-hole pixel, " +
                  "every non-hole pixel covered, hole cells cleared; MPHD 0x80, the 3 height textures uploaded, height textures at layer indices " +
                  "0 to 4 (chunk 18: 2, 3 and 4) -- " + string.Join("; ", results.ToArray()), log);
            Check(heightsDecide && deciding.Count == 4,
                  "terrain composite: where the height textures decide (chunks 10, 14, 17, 18) the GPU is far from plain weights and from the " +
                  "heights-off mix: " + string.Join("; ", deciding.ToArray()), log);

            // ---- S == 0: chunk 14's heights are both 0 in places; the mix falls back to the raw weights there (the GPU matched
            // the CPU at every pixel above, these included)
            {
                TerrainChunkMaterial c = mat.Chunks[14];
                int s = 128, fallback = 0, total = 0;
                var w = new float[2];
                var hk = new float[2];
                var res = new float[2];
                for (int y = 0; y < s; y++)
                    for (int x = 0; x < s; x++)
                    {
                        float u = (x + 0.5f) * 8f / s, v = (y + 0.5f) * 8f / s;
                        w[1] = TerrainCompositor.AlphaAt(c.Alpha, 1, u, v);
                        w[0] = 1f - w[1];
                        for (int k = 0; k < 2; k++) hk[k] = TerrainCompositor.HeightAt(c, c.Layers[k], cpu, u, v);
                        total++;
                        if (!TerrainCompositor.HeightMixWeights(w, hk, 2, res)) fallback++;
                    }
                Check(fallback > 0 && fallback < total && allWithin,
                      "terrain composite: S = 0 -- chunk 14's two height textures are both 0 at " + fallback + " of " + total + " pixels, where the " +
                      "height mix falls back to the raw weights; the GPU equals the CPU there too", log);
            }

            // ---- MCCV above 1: chunk 2's composite exceeds 1 somewhere, and the validation dump keeps it (unclamped)
            int above = 0;
            if (chunk2 != null)
                for (int i = 0; i < chunk2.Length; i += 4)
                    if (chunk2[i] > 1f || chunk2[i + 1] > 1f || chunk2[i + 2] > 1f) above++;
            Check(above > 0, "terrain composite: MCCV takes chunk 2's composite above 1 at " + above + " pixel(s), kept in the validation dump", log);

            // ---- the production clamp and clippedPixels, GPU against CPU: chunk 2 again with the clamp on (validation sampling:
            // the clamp does not depend on the sampling)
            if (chunk2 != null)
            {
                int s;
                string why;
                float[] clamped = RenderChunk(rt, mesh, mat.Chunks[2], true, false, out s, out why);
                float[] cpuSat = TerrainCompositor.ComposeGrid(mat.Chunks[2], cpu, size2, true);
                int u2, h2;
                float err = clamped != null ? ChunkError(clamped, cpuSat, size2, 0UL, out u2, out h2) : float.PositiveInfinity;
                int overOne = clamped != null ? WmvTerrainComposite.Clipped(clamped) : -1;
                // clippedPixels of the unclamped render against the CPU's pixels above 1; a pixel within a half-float step of 1 may
                // count either way.
                const float HalfStep = 1f / 1024f;
                int cpuOver = 0, cpuNear = 0;
                for (int i = 0; i < chunk2Cpu.Length; i += 4)
                {
                    float m = Math.Max(chunk2Cpu[i], Math.Max(chunk2Cpu[i + 1], chunk2Cpu[i + 2]));
                    if (m > 1f + HalfStep) cpuOver++;
                    if (m > 1f - HalfStep) cpuNear++;
                }
                int clipped = WmvTerrainComposite.Clipped(chunk2);
                Check(err <= 0.5f && overOne == 0 && cpuOver > 0 && clipped >= cpuOver && clipped <= cpuNear,
                      string.Format("terrain composite: the PRODUCTION CLAMP -- chunk 2 rendered with the shader's saturate equals the CPU's saturated " +
                                    "composite within 0.5/255 (max {0:0.###}/255) with no value above 1 ({1}); clippedPixels {2} of the unclamped " +
                                    "render against the CPU's {3} pixels above 1 ({4} within a half-float step of it)", err, overOne, clipped, cpuOver, cpuNear), log);
            }

            // ---- the 8 symmetries: only the identity fits
            if (chunk2 != null)
            {
                var fits = new List<string>();
                bool onlyIdentity = true;
                for (int s = 0; s < 8; s++)
                {
                    int u, h;
                    float err = ChunkError(Symmetry(chunk2, size2, s), chunk2Cpu, size2, 0UL, out u, out h);
                    fits.Add(s + " " + err.ToString("0.##"));
                    if (s == 0) onlyIdentity &= err <= 0.5f;
                    else onlyIdentity &= err > 10f;
                }
                Check(onlyIdentity, "terrain composite: of the 8 symmetries of chunk 2's dump only the identity fits the CPU (max /255: " +
                      string.Join(", ", fits.ToArray()) + ")", log);
            }

            // ---- production sampling with every layer at one texel per pixel (chunk 13: three layers at 32 texels per chunk)
            {
                float x0, zn, ex, ez;
                WmvTerrainComposite.ChunkRegion(mesh, 13, out x0, out zn, out ex, out ez);
                Action restore;
                float[] gpu = WmvTerrainComposite.RenderToArray(WmvTerrainComposite.ChunkDraws(rt, 13, out restore), x0, zn, ex, ez, 40f, 60f, 32, 32,
                                                                false, true);
                restore();
                int u, h;
                float err = ChunkError(gpu, TerrainCompositor.ComposeGrid(mat.Chunks[13], cpu, 32, false), 32, 0UL, out u, out h);
                Check(err <= 2f && u == 0, "terrain composite: production sampling (the hardware's own filtering) with every layer at one texel " +
                      "per pixel is within 2/255 of the CPU (chunk 13, three layers: max " + err.ToString("0.###") + "/255)", log);
            }

            // ---- the whole tile at 32 pixels per chunk: placement and orientation of every chunk (the T4-only chunks 19 and up
            // carry a colour of their own, so a chunk in the wrong place shows)
            {
                float t = (float)TerrainCoordinates.TileSize;
                Action restore;
                float[] tile = WmvTerrainComposite.RenderToArray(WmvTerrainComposite.ChunkDraws(rt, -1, out restore), 0f, 0f, t, t, 40f, 60f, 512, 512,
                                                                 true, true);
                restore();
                int placed = 0, checkedChunks = 0, outside = 0;
                float worst = 0f;
                for (int n = 19; n < 256; n++)
                {
                    float[] cpuImg = TerrainCompositor.ComposeGrid(mat.Chunks[n], cpu, 32, false);
                    int ix = n % 16, iy = n / 16;
                    float e = 0f;
                    for (int y = 0; y < 32; y++)
                        for (int x = 0; x < 32; x++)
                        {
                            int o = 4 * ((iy * 32 + y) * 512 + ix * 32 + x), c0 = 4 * (y * 32 + x);
                            if (tile[o + 3] != 1f) outside++;
                            for (int c = 0; c < 3; c++)
                                e = Math.Max(e, Diff(tile[o + c], cpuImg[c0 + c]) * 255f);
                        }
                    checkedChunks++;
                    if (e <= 0.5f) placed++;
                    worst = Math.Max(worst, e);
                }
                int neutralCovered = 0;
                int[] neutralChunks = { 7, 8, 9, 12, 15, 16 };
                foreach (int n in neutralChunks)
                    for (int y = 0; y < 32; y++)
                        for (int x = 0; x < 32; x++)
                            if (tile[4 * (((n / 16) * 32 + y) * 512 + (n % 16) * 32 + x) + 3] != 0f) neutralCovered++;
                Check(placed == checkedChunks && outside == 0 && neutralCovered == 0,
                      "terrain composite: the 512-px tile composite puts every chunk where it belongs, north row first, west column first (" + placed +
                      " of " + checkedChunks + " single-layer chunks within 0.5/255 of the CPU in their own square, max " + worst.ToString("0.###") +
                      "/255; " + outside + " of their pixels uncovered); the " + neutralChunks.Length + " neutral chunks (Malformed, AlphaUnsupported, " +
                      "LayerCountUnsupported, NoLayers, HeightTextureMissing, LayerEffectUnresolved) are not drawn into it (" + neutralCovered + " px)", log);
            }

            // ---- the missing-texture checker is in chunk 5's dump (magenta and black where its weight is 1)
            {
                float x0, zn, ex, ez;
                WmvTerrainComposite.ChunkRegion(mesh, 5, out x0, out zn, out ex, out ez);
                Action restore;
                float[] gpu = WmvTerrainComposite.RenderToArray(WmvTerrainComposite.ChunkDraws(rt, 5, out restore), x0, zn, ex, ez, 40f, 60f, 128, 128,
                                                                true, true);
                restore();
                // Column 0 of the alpha has 252/255 of the missing layer: the pixels there are nearly the checker.
                int magenta = 0, black = 0;
                for (int y = 0; y < 128; y++)
                {
                    int o = 4 * (y * 128 + 0);
                    if (gpu[o] > 0.9f && gpu[o + 1] < 0.1f && gpu[o + 2] > 0.9f) magenta++;
                    else if (gpu[o] < 0.1f && gpu[o + 1] < 0.1f && gpu[o + 2] < 0.1f) black++;
                }
                Check(magenta > 30 && black > 30 && mat.Chunks[5].Layers[1].Missing,
                      "terrain composite: a layer naming texture id 0 is drawn as the magenta/black checker (" + magenta + " magenta, " + black +
                      " black of 128 pixels down the chunk's west edge)", log);
            }
        }
        finally
        {
            rt.DisposeImmediate();
        }

        // ---- MPHD without 0x80: the same bytes, H = 1 (no height texture read or uploaded), GPU against CPU
        {
            var off = new WmvRuntimeTerrainTile(0, tx, ty, 9002);
            try
            {
                WmvTerrainBuilder.Build(off, meshOff, contentOff, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = -1 }, null);
                off.Root.SetActive(true);
                Func<uint, TerrainTexels> cpu = GpuTexels(off);
                bool noHeights = true;
                foreach (uint hid in MatHeightTextures) noHeights &= !off.Textures.ContainsKey(hid);
                var results = new List<string>();
                bool within = true;
                foreach (int n in new[] { 10, 14, 15, 17, 18 })
                {
                    TerrainChunkMaterial c = matOff.Chunks[n];
                    int s, u, h;
                    string why;
                    float[] gpu = RenderChunk(off, meshOff, c, true, true, out s, out why);
                    if (gpu == null)
                    {
                        within = false;
                        results.Add(n + ": " + why);
                        continue;
                    }
                    float err = ChunkError(gpu, TerrainCompositor.ComposeGrid(c, cpu, s, false), s, c.HoleMask, out u, out h);
                    within &= err <= 0.5f && u == 0 && c.State == TerrainMaterialState.Textured && !c.UseHeightTextures;
                    results.Add(string.Format("{0} ({1}, {2} px): max {3:0.###}/255", n, c.State, s, err));
                }
                Check(within && noHeights,
                      "terrain composite: MPHD 0x4 (no 0x80) -- the height-texture chunks are Textured, no height texture is uploaded, and the GPU " +
                      "draws the height mix with H = 1 equal to the CPU within 0.5/255: " + string.Join("; ", results.ToArray()), log);
            }
            finally
            {
                off.DisposeImmediate();
            }
        }

        // ---- the plain-weights diagnostic (texturedplainweights): every chunk with layers in plain weights under the hatch,
        // the plain-diagnostic ones included; GPU against TerrainCompositor's PlainWeights away from the hatch lines, the hatch
        // the plain colour darkened
        {
            TerrainTileMesh meshP;
            Dictionary<uint, byte[]> blpsP;
            WmvTerrainTileContent contentP = MaterialTestContent(tx, ty, MatHeightsOn, out meshP, out blpsP);
            var pv = new WmvRuntimeTerrainTile(0, tx, ty, 9003);
            try
            {
                WmvTerrainBuilder.Build(pv, meshP, contentP, null, new WmvTerrainBuildOptions { View = WmvTerrainView.TexturedPlainWeights, OnlyChunk = -1 }, null);
                pv.Root.SetActive(true);
                Func<uint, TerrainTexels> cpu = GpuTexels(pv);
                bool noHeights = true;
                foreach (uint hid in MatHeightTextures) noHeights &= !pv.Textures.ContainsKey(hid);
                var results = new List<string>();
                bool within = true;
                int allHatched = 0;
                foreach (int n in new[] { 2, 10, 14, 15, 16 })
                {
                    TerrainChunkMaterial c = contentP.Material.Chunks[n];
                    int s;
                    string why;
                    float[] gpu = RenderChunk(pv, meshP, c, true, true, out s, out why, true);
                    if (gpu == null)
                    {
                        within = false;
                        results.Add(n + ": " + why);
                        continue;
                    }
                    float[] plain = TerrainCompositor.ComposeGrid(c, cpu, s, false, TerrainBlendRule.PlainWeights);
                    float worstPlain = 0f, worstHatch = 0f;
                    int hatched = 0, plainPx = 0;
                    for (int y = 0; y < s; y++)
                        for (int x = 0; x < s; x++)
                        {
                            float u = (x + 0.5f) * 8f / s, v = (y + 0.5f) * 8f / s;
                            if (TerrainCompositor.IsHole(c.HoleMask, u, v))
                                continue;
                            float f = (u + v) * 2f;
                            f -= (float)Math.Floor(f);
                            int o = 4 * (y * s + x);
                            if (f > 0.005f && f < 0.135f)
                            {
                                hatched++;
                                for (int k = 0; k < 3; k++)
                                {
                                    float dark = k == 2 ? 0.09f : 0.08f;
                                    worstHatch = Math.Max(worstHatch, Diff(gpu[o + k], plain[o + k] * 0.35f + dark * 0.65f) * 255f);
                                }
                            }
                            else if (f > 0.145f && f < 0.995f)
                            {
                                plainPx++;
                                for (int k = 0; k < 3; k++)
                                    worstPlain = Math.Max(worstPlain, Diff(gpu[o + k], plain[o + k]) * 255f);
                            }
                        }
                    // (At 64 px every hatch sample sits exactly on a line's edge, where the test does not judge it.)
                    within &= worstPlain <= 0.5f && worstHatch <= 1f && plainPx > 0;
                    allHatched += hatched;
                    results.Add(string.Format("{0} ({1}): {2} plain px max {3:0.###}/255, {4} hatch px max {5:0.###}/255", n, c.State, plainPx, worstPlain,
                                              hatched, worstHatch));
                }
                Check(within && noHeights && allHatched > 0,
                      "terrain composite: texturedplainweights draws PLAIN WEIGHTS (no cutoff, no heights; no height texture uploaded) equal to " +
                      "TerrainCompositor's within 0.5/255 off the hatch, the hatch lines that colour darkened (within 1/255), the plain-diagnostic " +
                      "chunks included: " + string.Join("; ", results.ToArray()), log);
            }
            finally
            {
                pv.DisposeImmediate();
            }
        }
        Check(WmvRuntimeTerrainTile.LiveTextures == 0 && WmvRuntimeTerrainTile.LiveTextureBytes == 0 && WmvRuntimeTerrainTile.LiveAlphaArrays == 0 &&
              TerrainTallies() == "0/0/0",
              "terrain composite: the tiles' textures, alpha arrays, materials and meshes are released (tallies " + TerrainTallies() + ", textures " +
              WmvRuntimeTerrainTile.LiveTextures + ", alpha arrays " + WmvRuntimeTerrainTile.LiveAlphaArrays + ")", log);
    }

    // ================================================================ up to 8 layers, MTXP scale nibble 4, the sampler carrier

    // A second synthetic tex0 over the same root and textures (MaterialTestTextures). MDID entries (index: texture, MTXP scale
    // nibble -> repeats per chunk; MHID height texture; MTXP height scale, offset):
    //    0 T9 n0 -> 8;   H9 (0.7, 0.4)       6 T8 n2 -> 2;   H8 (3.0, 0.25)       12 T2 n1 -> 4; H9 (1.5, 0.1)
    //    1 T2 n1 -> 4;   H9 (1.5, 0.1)       7 T3 n4 -> 0.5; H8 (1.0, 0.2)        13 T7 n1 -> 4; H8 (1.2, 0.05)
    //    2 T3 n3 -> 1;   H6 (0.8, 0.3)       8 T2 n4 -> 0.5; H6 (0.9, 0.15)       14 T8 n2 -> 2; H6 (3.0, 0.25)
    //    3 T4 n0 -> 8;   H8 (2.0, 0.05)      9 T1 n0 -> 8;   H8 (0.6, 0.2)        15 T3 n3 -> 1; no MHID (0.8, 0.3)
    //    4 T5 n2 -> 2;   H9 (0.5, 0.5)      10 texture id 0, n1 -> 4; H9 (2, 0.1)  16 T4 n0 -> 8; no MHID (2.0, 0.6)
    //    5 T7 n1 -> 4;   H6 (1.2, 0.05)     11 T5 n2 -> 2;   H6 (0.5, 0.5)
    // Entries 10..16: every texture (diffuse and height) at 32 texels per chunk, for production sampling at one texel per pixel.
    static readonly uint[] R3Mdid = { 991009, 991002, 991003, 991004, 991005, 991007, 991008, 991003, 991002, 991001, 0, 991005, 991002, 991007,
                                      991008, 991003, 991004 };
    static readonly uint[] R3Mhid = { 992009, 992009, 992006, 992008, 992009, 992006, 992008, 992008, 992006, 992008, 992009, 992006, 992009,
                                      992008, 992006, 0, 0 };
    static readonly int[] R3Nibble = { 0, 1, 3, 0, 2, 1, 2, 4, 4, 0, 1, 2, 1, 1, 2, 3, 0 };
    /// <summary>Each entry's repeats per chunk by hand: 8 / 2^nibble.</summary>
    static readonly float[] R3Repeats = { 8f, 4f, 1f, 8f, 2f, 4f, 2f, 0.5f, 0.5f, 8f, 4f, 2f, 4f, 4f, 2f, 1f, 8f };
    static readonly float[] R3HeightScale = { 0.7f, 1.5f, 0.8f, 2f, 0.5f, 1.2f, 3f, 1f, 0.9f, 0.6f, 2f, 0.5f, 1.5f, 1.2f, 3f, 0.8f, 2f };
    static readonly float[] R3HeightOffset = { 0.4f, 0.1f, 0.3f, 0.05f, 0.5f, 0.05f, 0.25f, 0.2f, 0.15f, 0.2f, 0.1f, 0.5f, 0.1f, 0.05f, 0.25f, 0.3f,
                                               0.6f };
    /// <summary>The second tex0's chunks (R3Chunk) and their layers' MDID entries, layer 0 first.</summary>
    static readonly int[] R3Chunks = { 1, 17, 34, 48, 66 };
    static readonly int[][] R3Layers = { new[] { 7, 9, 8 }, new[] { 3, 7, 8 }, new[] { 11, 10, 12 }, new[] { 0, 1, 2, 3, 4, 5, 6, 7 },
                                         new[] { 12, 11, 13, 14, 15, 16 } };

    /// <summary>
    /// The second tex0's chunks (MCCV from the root on 17 and up):
    ///    1 (ix 1, iy 0)  E7 (T3, 0.5 repeats) + E9 (T1) + E8 (T2, 0.5 repeats): nibble 4 at phase (0.5, 0)
    ///   17 (ix 1, iy 1)  E3 (T4) + E7 + E8: nibble 4 at phase (0.5, 0.5)
    ///   34               E11 (T5) + E10 (texture id 0 with height texture H9: the sampler carrier's slot) + E12 (T2)
    ///   48 (ix 0, iy 3)  E0..E7: EIGHT layers (5..7 from the second alpha slice), a height texture and MTXP heights other than
    ///                    the default on every one, E7 (layer 7) at phase (0, 0.5)
    ///   66               E12, E11, E13, E14, E15, E16: SIX layers, every texture at 32 texels per chunk
    ///   any other        E15 alone
    /// Alpha sums at most 245.
    /// </summary>
    static AdtSynthetic.TexChunkSpec R3Chunk(int n)
    {
        int at = Array.IndexOf(R3Chunks, n);
        if (at < 0)
            return AdtSynthetic.TexChunkSpec.Of(AdtSynthetic.Layer(15));
        int[] e = R3Layers[at];
        var layers = new AdtSynthetic.TexLayerSpec[e.Length];
        layers[0] = AdtSynthetic.Layer((uint)e[0]);
        for (int k = 1; k < e.Length; k++)
        {
            int kk = k;
            Func<int, int, int> a;
            switch (n)
            {
                case 1: a = kk == 1 ? (Func<int, int, int>)((r, c) => (r + 2 * c) % 40) : (r, c) => 30 + (3 * r + c) % 50; break;
                case 17: a = kk == 1 ? (Func<int, int, int>)((r, c) => 100 + (r * c) % 60) : (r, c) => (r + c) % 40; break;
                case 34: a = kk == 1 ? (Func<int, int, int>)((r, c) => 60 + (2 * r + c) % 80) : (r, c) => (3 * r) % 50; break;
                case 48: a = (r, c) => (r * 3 + c * 5 + 17 * kk) % 36; break;
                default: a = (r, c) => (r * 2 + c * 3 + 11 * kk) % 45; break;
            }
            layers[k] = AdtSynthetic.Layer((uint)e[k], k % 2 == 0 ? AlphaStorage.Uncompressed8 : AlphaStorage.Compressed, AlphaOf(a));
        }
        return AdtSynthetic.TexChunkSpec.Of(layers);
    }

    static byte[] R3Tex0()
    {
        var mtxp = new List<byte>();
        for (int i = 0; i < R3Mdid.Length; i++)
            mtxp.AddRange(AdtSynthetic.MtxpEntry((uint)(R3Nibble[i] << 4), R3HeightScale[i], R3HeightOffset[i]));
        return AdtSynthetic.Tex0(new AdtSynthetic.Tex0Spec { Mdid = R3Mdid, Mhid = R3Mhid, Mtxp = mtxp.ToArray(), Chunk = R3Chunk });
    }

    /// <summary>The second synthetic tile with MPHD `mphd`: the first's root, R3Tex0, the material, every texture prepared,
    /// the failures (none) settled.</summary>
    static WmvTerrainTileContent R3Content(int tx, int ty, uint mphd, out TerrainTileMesh mesh)
    {
        byte[] rootBytes = MaterialTestRoot(tx, ty);
        byte[] tex0 = R3Tex0();
        AdtRoot root = AdtParser.ParseRoot(rootBytes, tx, ty);
        mesh = TerrainMeshBuilder.Build(root, tx, ty, WorldToUnity.TileOrigin(tx, ty));
        var content = new WmvTerrainTileContent
        {
            Material = TerrainMaterialBuilder.Build(root, AdtParser.ParseTex(tex0), mphd), Tex0FileDataID = MatTex0,
            MapTextureFileDataID = MatMapTexture, MphdFlags = mphd, Build = "0.0.4.1", RootMd5 = WmvTerrainSession.Md5Hex(rootBytes),
            Tex0Md5 = WmvTerrainSession.Md5Hex(tex0),
        };
        foreach (KeyValuePair<uint, byte[]> kv in MaterialTestTextures())
            content.Textures[kv.Key] = PrepareTexture(kv.Key, kv.Value);
        List<uint> failedDiffuse, failedHeight;
        content.SettleFailures(out failedDiffuse, out failedHeight);
        return content;
    }

    /// <summary>A copy of chunk material `c` for the CPU twin with every layer's (PhaseU, PhaseV) replaced by `phase(layer)`.</summary>
    static TerrainChunkMaterial WithPhases(TerrainChunkMaterial c, Func<TerrainMaterialLayer, Vector2> phase)
    {
        var layers = new TerrainMaterialLayer[c.Layers.Length];
        for (int k = 0; k < layers.Length; k++)
        {
            TerrainMaterialLayer l = c.Layers[k];
            Vector2 p = phase(l);
            layers[k] = new TerrainMaterialLayer
            {
                Layer = l.Layer, MdidIndex = l.MdidIndex, IndexInRange = l.IndexInRange, TextureFileDataID = l.TextureFileDataID, Flags = l.Flags,
                HasMtxp = l.HasMtxp, MtxpFlags = l.MtxpFlags, ScaleExponent = l.ScaleExponent, RepeatsPerChunk = l.RepeatsPerChunk, PhaseU = p.x,
                PhaseV = p.y, HeightScale = l.HeightScale, HeightOffset = l.HeightOffset, MtxpWord3 = l.MtxpWord3,
                HeightTextureFileDataID = l.HeightTextureFileDataID, Storage = l.Storage, Missing = l.Missing, HeightMissing = l.HeightMissing,
            };
        }
        return new TerrainChunkMaterial
        {
            Index = c.Index, State = c.State, Reason = c.Reason, Layers = layers, Alpha = c.Alpha, AlphaSlice = c.AlphaSlice,
            AlphaSlice2 = c.AlphaSlice2, VertexColors = c.VertexColors, HoleMask = c.HoleMask, UseHeightTextures = c.UseHeightTextures,
        };
    }

    static void TerrainLayersPhaseCarrierTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        TerrainTileMesh mesh, meshOff;
        WmvTerrainTileContent content = R3Content(tx, ty, MatHeightsOn, out mesh);
        WmvTerrainTileContent contentOff = R3Content(tx, ty, MatHeightsOff, out meshOff);
        TerrainTileMaterial mat = content.Material, matOff = contentOff.Material;

        // ---- (precondition) the second tile as built, against hand values: states, layers and their MDID entries, repeats
        // (8 / 2^n), phases (frac(ix * r), frac(iy * r) with ix = n % 16, iy = n / 16: half a repeat on an odd column or row
        // at 0.5 repeats, 0 for whole repeats), MTXP heights, and a second alpha slice right after the first for 6 and 8 layers
        {
            var wrong = new List<string>();
            var phases = new List<string>();
            for (int i = 0; i < R3Chunks.Length; i++)
            {
                int n = R3Chunks[i];
                int[] e = R3Layers[i];
                TerrainChunkMaterial c = mat.Chunks[n];
                TerrainMaterialState want = n == 34 ? TerrainMaterialState.MissingTexture : TerrainMaterialState.Textured;
                bool ok = c.State == want && c.Layers.Length == e.Length && c.UseHeightTextures && c.AlphaSlice >= 0 &&
                          (e.Length > 5 ? c.AlphaSlice2 == c.AlphaSlice + 1 && c.AlphaSliceCount == 2 : c.AlphaSlice2 == -1 && c.AlphaSliceCount == 1) &&
                          matOff.Chunks[n].State == want && !matOff.Chunks[n].UseHeightTextures;
                for (int k = 0; ok && k < e.Length; k++)
                {
                    TerrainMaterialLayer l = c.Layers[k];
                    float r = R3Repeats[e[k]];
                    float pu = r == 0.5f ? 0.5f * (n % 16 % 2) : 0f, pv = r == 0.5f ? 0.5f * (n / 16 % 2) : 0f;
                    ok &= l.MdidIndex == e[k] && l.TextureFileDataID == R3Mdid[e[k]] && l.HeightTextureFileDataID == R3Mhid[e[k]] &&
                          l.RepeatsPerChunk == r && l.PhaseU == pu && l.PhaseV == pv && l.HeightScale == R3HeightScale[e[k]] &&
                          l.HeightOffset == R3HeightOffset[e[k]] && l.Missing == (R3Mdid[e[k]] == 0);
                    if (r == 0.5f)
                        phases.Add(string.Format(System.Globalization.CultureInfo.InvariantCulture, "{0}/{1} ({2}, {3})", n, k, l.PhaseU, l.PhaseV));
                }
                if (!ok)
                    wrong.Add(n + ": " + c.State + " (" + c.Reason + "), " + c.Layers.Length + " layer(s), slices " + c.AlphaSlice + "/" + c.AlphaSlice2);
            }
            Check(wrong.Count == 0 && phases.Count == 5,
                  "terrain layers and phase: (precondition) the second synthetic tile as built equals the hand values -- chunks 1, 17, 34, 48, 66 " +
                  "with 3, 3, 3, 8 and 6 layers (34 missingTexture: texture id 0), every layer's MDID entry, texture, height texture, repeats " +
                  "8 / 2^n (0.5 for nibble 4) and MTXP heights; the nibble-4 layers' phases (chunk/layer (u, v)) " + string.Join(", ", phases.ToArray()) +
                  "; a second alpha slice right after the first for 48 and 66 only" + (wrong.Count > 0 ? "; WRONG: " + string.Join("; ", wrong.ToArray()) : ""),
                  log);
        }

        var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9050);
        Texture2D carrierRef = null;
        bool carrierMade = false;
        try
        {
            WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = -1 }, null);
            rt.Root.SetActive(true);
            carrierRef = rt.SamplerCarrier;
            carrierMade = carrierRef != null;
            Func<uint, TerrainTexels> cpu = GpuTexels(rt);
            Func<uint, Texture> uploaded = id =>
            {
                Texture2D t;
                return rt.Textures.TryGetValue(id, out t) ? t : null;
            };

            // ---- the GPU against TerrainCompositor (validation sampling, float target, unclamped): every one of these chunks
            // alone at its validation size within 0.5/255 at every non-hole pixel (the same gate as the first tile's)
            var results = new List<string>();
            bool within = true;
            var gpuOf = new Dictionary<int, float[]>();
            var sizeOf = new Dictionary<int, int>();
            foreach (int n in R3Chunks)
            {
                TerrainChunkMaterial c = mat.Chunks[n];
                int s, u, h;
                string why;
                float[] gpu = RenderChunk(rt, mesh, c, true, true, out s, out why);
                if (gpu == null)
                {
                    within = false;
                    results.Add(n + ": " + why);
                    continue;
                }
                float err = ChunkError(gpu, TerrainCompositor.ComposeGrid(c, cpu, s, false), s, c.HoleMask, out u, out h);
                within &= err <= 0.5f && u == 0 && h == 0;
                gpuOf[n] = gpu;
                sizeOf[n] = s;
                results.Add(string.Format("{0} ({1}, {2} layers, {3} px): max {4:0.###}/255{5}", n, c.State, c.Layers.Length, s, err,
                                          u > 0 ? ", " + u + " uncovered" : ""));
            }
            Check(within && gpuOf.Count == R3Chunks.Length,
                  "terrain layers and phase: the GPU equals TerrainCompositor within 0.5/255 at every pixel (validation sampling) -- EIGHT layers " +
                  "with a height texture and MTXP heights on each (layers 5..7 from the second alpha slice), SIX layers, MTXP scale nibble 4 " +
                  "(0.5 repeats) at phases (0.5, 0), (0.5, 0.5) and (0, 0.5), and a missing layer with a height texture: " +
                  string.Join("; ", results.ToArray()), log);

            // ---- the phase decides: the CPU without the phases, or with u and v swapped, is far from the GPU
            {
                var phaseResults = new List<string>();
                bool decides = true;
                foreach (int n in new[] { 1, 17, 48 })
                {
                    if (!gpuOf.ContainsKey(n))
                    {
                        decides = false;
                        continue;
                    }
                    TerrainChunkMaterial c = mat.Chunks[n];
                    int s = sizeOf[n], u, h;
                    float zero = ChunkError(gpuOf[n], TerrainCompositor.ComposeGrid(WithPhases(c, l => Vector2.zero), cpu, s, false), s, c.HoleMask, out u, out h);
                    float swapped = ChunkError(gpuOf[n], TerrainCompositor.ComposeGrid(WithPhases(c, l => new Vector2(l.PhaseV, l.PhaseU)), cpu, s, false), s,
                                               c.HoleMask, out u, out h);
                    bool symmetric = n == 17;      // phase (0.5, 0.5): swapping u and v changes nothing
                    decides &= zero > 2f && (symmetric || swapped > 2f);
                    phaseResults.Add(string.Format("{0}: without the phase {1:0.#}/255, u and v swapped {2}", n, zero,
                                                   symmetric ? "(the same phase)" : swapped.ToString("0.#") + "/255"));
                }
                Check(decides && phaseResults.Count == 3,
                      "terrain layers and phase: the UV phase decides -- the CPU twin without it, or with u and v swapped, is far (> 2/255) from " +
                      "the GPU: " + string.Join("; ", phaseResults.ToArray()), log);
            }

            // ---- the heights decide on the eight-layer chunk: neither plain weights nor the same bytes without MPHD 0x80 fit
            if (gpuOf.ContainsKey(48))
            {
                TerrainChunkMaterial c = mat.Chunks[48];
                int s = sizeOf[48], u, h;
                float plain = ChunkError(gpuOf[48], TerrainCompositor.ComposeGrid(c, cpu, s, false, TerrainBlendRule.PlainWeights), s, c.HoleMask, out u, out h);
                float off = ChunkError(gpuOf[48], TerrainCompositor.ComposeGrid(matOff.Chunks[48], cpu, s, false), s, c.HoleMask, out u, out h);
                Check(plain > 2f && off > 2f,
                      string.Format("terrain layers and phase: the eight-layer chunk's height textures decide -- plain weights {0:0.#}/255, without the " +
                                    "heights {1:0.#}/255 from the GPU", plain, off), log);
            }
            else
                Check(false, "terrain layers and phase: the eight-layer chunk was not rendered", log);

            // ---- the packing: every slot k = 0..7 of every material against its layer (texture or the carrier, repeats, phase,
            // missing flag, height texture and MTXP heights; unused slots at the defaults), the layer count and both slices
            {
                var bad = new List<string>();
                int slots = 0, heightSlots = 0, carrierSlots = 0;
                foreach (int n in R3Chunks)
                {
                    TerrainChunkMaterial c = mat.Chunks[n];
                    Material m = rt.ChunkMaterials[n];
                    bool ok = m != null && rt.ChunkTextured[n] && (int)m.GetFloat(WmvTerrainMaterials.LayerCountId) == c.Layers.Length &&
                              (int)m.GetFloat(WmvTerrainMaterials.AlphaSliceId) == c.AlphaSlice &&
                              (int)m.GetFloat(WmvTerrainMaterials.AlphaSlice2Id) == Math.Max(0, c.AlphaSlice2) &&
                              m.GetFloat(WmvTerrainMaterials.UseHeightsId) == 1f && m.GetTexture(WmvTerrainMaterials.AlphaId) == rt.AlphaArray;
                    for (int k = 0; ok && k < WmvTerrainMaterials.ShaderLayers; k++)
                    {
                        float rep = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.RepeatsIds, k);
                        float pu = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.PhaseUIds, k);
                        float pv = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.PhaseVIds, k);
                        float miss = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.MissingIds, k);
                        float has = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.HasHeightIds, k);
                        float scale = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.HeightScaleIds, k);
                        float offset = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.HeightOffsetIds, k);
                        Texture tex = m.GetTexture(WmvTerrainMaterials.LayerIds[k]), ht = m.GetTexture(WmvTerrainMaterials.HeightIds[k]);
                        if (k >= c.Layers.Length)
                        {
                            ok &= rep == 8f && pu == 0f && pv == 0f && miss == 0f && has == 0f && scale == 0f && offset == 1f;
                            continue;
                        }
                        TerrainMaterialLayer l = c.Layers[k];
                        slots++;
                        ok &= rep == l.RepeatsPerChunk && pu == l.PhaseU && pv == l.PhaseV && scale == l.HeightScale && offset == l.HeightOffset;
                        if (l.Missing)
                        {
                            ok &= miss == 1f && tex != null && tex == rt.SamplerCarrier;
                            carrierSlots++;
                        }
                        else
                            ok &= miss == 0f && tex != null && tex == uploaded(l.TextureFileDataID);
                        if (l.HeightTextureFileDataID != 0)
                        {
                            ok &= has == 1f && ht != null && ht == uploaded(l.HeightTextureFileDataID);
                            heightSlots++;
                        }
                        else
                            ok &= has == 0f;
                        if (!ok)
                            bad.Add(n + "/" + k);
                    }
                    if (!ok && (bad.Count == 0 || !bad[bad.Count - 1].StartsWith(n + "/", StringComparison.Ordinal)))
                        bad.Add(n.ToString());
                }
                Check(bad.Count == 0 && slots == 23 && heightSlots == 21 && carrierSlots == 1,
                      "terrain layers and phase: every material slot k = 0..7 holds its layer's values (textures, repeats, phase u and v, missing flag, " +
                      "height texture, MTXP height scale and offset; the unused slots the defaults), the layer count and both alpha slices -- " + slots +
                      " layer slots, " + heightSlots + " reading a height texture (all 8 of the eight-layer chunk), " + carrierSlots + " holding the " +
                      "sampler carrier" + (bad.Count > 0 ? "; WRONG: " + string.Join(", ", bad.ToArray()) : ""), log);
            }

            // ---- the sampler carrier: the layer textures' sampler settings, apart from the texture tallies; in production
            // sampling (every texture at one texel per pixel) the missing layer's height texture is read with the carrier's
            // sampler: within the production limit of 2/255 of the CPU, and outside it with the carrier's wrap mode Clamp
            {
                Texture2D carrier = rt.SamplerCarrier;
                Texture2D t5;
                rt.Textures.TryGetValue(991005, out t5);
                bool settings = carrier != null && t5 != null && carrier.wrapMode == TextureWrapMode.Repeat && carrier.filterMode == FilterMode.Trilinear &&
                                carrier.anisoLevel == WmvTerrainMaterials.LayerAnisoLevel && carrier.mipMapBias == 0f && carrier.mipmapCount > 1 &&
                                !carrier.isReadable && carrier.wrapMode == t5.wrapMode && carrier.filterMode == t5.filterMode &&
                                carrier.anisoLevel == t5.anisoLevel && carrier.mipMapBias == t5.mipMapBias;
                bool apart = carrier != null && !rt.Textures.ContainsValue(carrier) && rt.TextureCount == rt.Textures.Count &&
                             WmvRuntimeTerrainTile.LiveTextures == rt.Textures.Count;
                Func<int, float> production = n =>
                {
                    float x0, zn, ex, ez;
                    WmvTerrainComposite.ChunkRegion(mesh, n, out x0, out zn, out ex, out ez);
                    Action restore;
                    float[] gpu = WmvTerrainComposite.RenderToArray(WmvTerrainComposite.ChunkDraws(rt, n, out restore), x0, zn, ex, ez, 40f, 60f, 32, 32,
                                                                    false, true);
                    restore();
                    int u, h;
                    float e = ChunkError(gpu, TerrainCompositor.ComposeGrid(mat.Chunks[n], cpu, 32, false), 32, mat.Chunks[n].HoleMask, out u, out h);
                    return u == 0 ? e : float.PositiveInfinity;
                };
                float p34 = production(34), p66 = production(66), clamp34 = float.NaN, back34 = float.NaN;
                if (carrier != null)
                {
                    carrier.wrapMode = TextureWrapMode.Clamp;
                    try { clamp34 = production(34); }
                    finally { carrier.wrapMode = TextureWrapMode.Repeat; }
                    back34 = production(34);
                }
                Check(settings && apart && p34 <= 2f && p66 <= 2f && clamp34 > 2f && back34 <= 2f,
                      string.Format("terrain layers and phase: the SAMPLER CARRIER ({0}) has the layer textures' sampler settings (Repeat, trilinear, " +
                                    "aniso {1}, mip bias 0, {2} mips, no CPU copy) and is in no texture tally; production sampling with every texture " +
                                    "at one texel per pixel is within 2/255 of the CPU for chunk 34 (its missing layer's height texture read through " +
                                    "the carrier's slot: max {3:0.###}/255) and the six-layer chunk 66 (max {4:0.###}/255); with the carrier set to " +
                                    "Clamp chunk 34 is {5:0.#}/255 off, back at Repeat {6:0.###}/255", carrier != null ? carrier.name : "none",
                                    WmvTerrainMaterials.LayerAnisoLevel, carrier != null ? carrier.mipmapCount : 0, p34, p66, clamp34, back34), log);
            }
        }
        finally
        {
            rt.DisposeImmediate();
        }
        Check(carrierMade && carrierRef == null && WmvRuntimeTerrainTile.LiveTextures == 0 && WmvRuntimeTerrainTile.LiveAlphaArrays == 0 && TerrainTallies() == "0/0/0",
              "terrain layers and phase: the tile, its textures, alpha array and sampler carrier are released (tallies " + TerrainTallies() + ")", log);
    }

    // ================================================================ the dumps

    [Serializable] class SidecarProbe
    {
        public string format;
        public string client;
        public string build;
        public int[] tile;
        public int tex0FileDataID;
        public string wdtMd5;
        public string rootMd5;
        public string tex0Md5;
        public string kind;
        public string sampling;
        public string colourDomain;
        public bool saturated;
        public int clippedPixels;
        public string view;
    }

    [Serializable] class TexelDumpProbe
    {
        public string format;
        public string client;
        public string build;
        public TexelEntryProbe[] textures;
    }

    [Serializable] class TexelEntryProbe
    {
        public int fileDataID;
        public string role;
        public string md5;
        public int width;
        public int height;
        public string raw;
    }

    static void TerrainDumpTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;

        // ---- refused outside the textured view: nothing written, the reason logged
        {
            var refusals = new List<string>();
            bool onlyTextured = WmvTerrainComposite.DumpRefusal(WmvTerrainView.Textured) == null;
            foreach (WmvTerrainView v in Enum.GetValues(typeof(WmvTerrainView)))
                if (v != WmvTerrainView.Textured)
                    onlyTextured &= WmvTerrainComposite.DumpRefusal(v) != null;
            TerrainTileMesh mesh;
            Dictionary<uint, byte[]> blps;
            WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
            var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9010);
            string name = "wmvselftest_refused_" + DateTime.UtcNow.Ticks;
            bool nothing;
            try
            {
                WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.TexturedLit, OnlyChunk = -1 }, null);
                WmvTerrainComposite.WriteTileComposite(rt, mesh, content, TexturedCommand(1, tx, ty), MatRoot, name, 64, false, null, s => refusals.Add(s));
                WmvTerrainComposite.WriteChunkComposites(rt, mesh, content, TexturedCommand(1, tx, ty), MatRoot, name, new List<int> { 2 }, false, null,
                                                         s => refusals.Add(s));
                nothing = !File.Exists(WmvTerrainComposite.PlayerFile(name + ".json")) && !File.Exists(WmvTerrainComposite.PlayerFile(name + ".raw")) &&
                          !File.Exists(WmvTerrainComposite.PlayerFile(name + "_c2.json")) && !File.Exists(WmvTerrainComposite.PlayerFile(name + "_c2.raw"));
            }
            finally
            {
                rt.DisposeImmediate();
            }
            Check(onlyTextured && nothing && refusals.Count == 2 && refusals.TrueForAll(s => s.Contains("refused") && s.Contains("texturedlit")),
                  "terrain dumps: refused outside the textured view (every other view has a reason), nothing written -- \"" +
                  string.Join("\"; \"", refusals.ToArray()) + "\"", log);
        }

        // ---- pinned to the authored composite: a tile in the plain-weights view (hatch on, view 2) whose materials also have
        // _WmvLinearOut 1 (the A/B display modes) renders, through ChunkDraws, the authored height mix -- and gets every value back
        {
            TerrainTileMesh mesh;
            Dictionary<uint, byte[]> blps;
            WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
            var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9011);
            try
            {
                WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.TexturedPlainWeights, OnlyChunk = -1 }, null);
                rt.Root.SetActive(true);
                foreach (Material m in rt.ChunkMaterials)
                    if (m != null && m.shader.name == WmvTerrainMaterials.ShaderName)
                        m.SetFloat(WmvTerrainMaterials.LinearOutId, 1f);
                Func<uint, TerrainTexels> cpu = GpuTexels(rt);
                TerrainChunkMaterial c = content.Material.Chunks[2];
                int s, s2, u, h;
                string why;
                float[] pinned = RenderChunk(rt, mesh, c, true, true, out s, out why);
                float[] asDrawn = RenderChunk(rt, mesh, c, true, true, out s2, out why, true);
                float[] cpuImg = TerrainCompositor.ComposeGrid(c, cpu, s, false);
                float errPinned = pinned != null ? ChunkError(pinned, cpuImg, s, 0UL, out u, out h) : float.PositiveInfinity;
                float errAsDrawn = asDrawn != null ? ChunkError(asDrawn, cpuImg, s, 0UL, out u, out h) : 0f;
                Material cm = rt.ChunkMaterials[2];
                bool restored = cm.GetFloat(WmvTerrainMaterials.LinearOutId) == 1f && cm.GetFloat(WmvTerrainMaterials.HatchId) == 1f &&
                                cm.GetFloat(WmvTerrainMaterials.ViewId) == WmvTerrainMaterials.ViewPlainWeights &&
                                cm.GetFloat(WmvTerrainMaterials.CullId) == (float)UnityEngine.Rendering.CullMode.Back;
                Check(errPinned <= 0.5f && errAsDrawn > 10f && restored,
                      string.Format("terrain dumps: the render pins _WmvLinearOut 0, _WmvHatch 0 and the textured view -- chunk 2 of a plain-weights tile " +
                                    "with the linear output on gives the authored height mix (max {0:0.###}/255 from the CPU; as drawn, {1:0.#}/255) and " +
                                    "its material gets linear output 1, hatch 1, view {2} and back-face culling back", errPinned, errAsDrawn,
                                    WmvTerrainMaterials.ViewPlainWeights), log);
            }
            finally
            {
                rt.DisposeImmediate();
            }
        }

        // ---- the tile composite streamed in bands equals one render; the sidecar and the texel dump's provenance
        {
            TerrainTileMesh mesh;
            Dictionary<uint, byte[]> blps;
            WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
            var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9012);
            try
            {
                WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = -1 }, null);
                bool wasActive = rt.Root.activeSelf;
                float t = (float)TerrainCoordinates.TileSize;
                float[] whole;
                rt.Root.SetActive(true);
                {
                    Action restore;
                    whole = WmvTerrainComposite.RenderToArray(WmvTerrainComposite.ChunkDraws(rt, -1, out restore), 0f, 0f, t, t, rt.Bounds.min.y,
                                                              rt.Bounds.max.y, 512, 512, true, true);
                    restore();
                }
                rt.Root.SetActive(wasActive);
                var keep = new float[512 * 512 * 4];
                int clipped, covered;
                byte[] bytes;
                using (var ms = new MemoryStream())
                {
                    WmvTerrainComposite.RenderTile(rt, 512, true, ms, keep, out clipped, out covered, 37);
                    bytes = ms.ToArray();
                }
                var streamed = new float[bytes.Length / 4];
                Buffer.BlockCopy(bytes, 0, streamed, 0, bytes.Length);
                int u, h;
                float err = streamed.Length == whole.Length ? ChunkError(streamed, whole, 512, 0UL, out u, out h) : float.PositiveInfinity;
                bool keptSame = true;
                for (int i = 0; i < keep.Length && i < streamed.Length; i++) keptSame &= keep[i] == streamed[i];
                int clippedP, coveredP;
                WmvTerrainComposite.RenderTile(rt, 512, false, null, null, out clippedP, out coveredP, 100);
                float[] unclampedProd;
                rt.Root.SetActive(true);
                {
                    Action restore;
                    unclampedProd = WmvTerrainComposite.RenderToArray(WmvTerrainComposite.ChunkDraws(rt, -1, out restore), 0f, 0f, t, t, rt.Bounds.min.y,
                                                                      rt.Bounds.max.y, 512, 512, false, true);
                    restore();
                }
                rt.Root.SetActive(wasActive);
                Check(err <= 0.5f && keptSame && covered == WmvTerrainComposite.Covered(whole) && clipped == WmvTerrainComposite.Clipped(whole) &&
                      clippedP == WmvTerrainComposite.Clipped(unclampedProd) && clippedP > 0 && coveredP == covered && rt.Root.activeSelf == wasActive,
                      string.Format("terrain dumps: the tile composite streamed in bands of 37 rows ({0} bytes) equals one 512-px render within " +
                                    "{1:0.###}/255, its PNG copy equals the stream, coverage {2} and clippedPixels {3} as the whole render's; in production " +
                                    "the unclamped pass counts {4} clipped pixels, as one unclamped render", bytes.Length, err, covered, clipped, clippedP), log);

                WmvIpcClient.TerrainTileCommand cmd = TexturedCommand(1, tx, ty);
                string side = WmvTerrainComposite.Sidecar(rt, content, cmd, MatRoot, "tile", -1, 512, 32, "production", "n.raw", "n.png", true, 3, "tex");
                SidecarProbe sp = JsonUtility.FromJson<SidecarProbe>(side);
                Check(sp != null && sp.format == "wmv-terrain-composite" && sp.client == cmd.Product && sp.build == content.Build &&
                      sp.wdtMd5 == content.WdtMd5 && sp.rootMd5 == content.RootMd5 && sp.tex0Md5 == content.Tex0Md5 && sp.tex0FileDataID == MatTex0 &&
                      sp.view == "textured" && sp.colourDomain == "authored" && sp.saturated && sp.clippedPixels == 3 && sp.tile != null &&
                      sp.tile.Length == 2 && sp.tile[0] == tx && content.WdtMd5.Length == 32 && content.Tex0Md5.Length == 32,
                      "terrain dumps: the sidecar carries the client build and the md5s of the WDT, root and tex0 the player parsed (build " +
                      (sp != null ? sp.build : "none") + ", wdt " + (sp != null ? sp.wdtMd5 : "") + ", root " + (sp != null ? sp.rootMd5 : "") + ", tex0 " +
                      (sp != null ? sp.tex0Md5 : "") + "), view textured, colour domain authored", log);

                string texels = WmvTerrainComposite.TexelDumpJson(rt, content, cmd, "tx");
                TexelDumpProbe tp = JsonUtility.FromJson<TexelDumpProbe>(texels);
                int diffuse = 0, height = 0;
                var badEntries = new List<string>();
                if (tp != null && tp.textures != null)
                    foreach (TexelEntryProbe e in tp.textures)
                    {
                        WmvTerrainTexture info;
                        content.Textures.TryGetValue((uint)e.fileDataID, out info);
                        bool isHeight = Array.IndexOf(MatHeightTextures, (uint)e.fileDataID) >= 0;
                        bool ok = info != null && e.md5 == info.Md5 && e.md5.Length == 32 && e.role == (isHeight ? "height" : "diffuse") &&
                                  e.raw == WmvTerrainComposite.TexelRawName("tx", (uint)e.fileDataID) && e.width == info.Width && e.height == info.Height;
                        if (isHeight) height++; else diffuse++;
                        if (!ok) badEntries.Add(e.fileDataID + " " + e.role + " " + e.md5);
                    }
                Check(tp != null && tp.format == "wmv-terrain-texels" && tp.build == content.Build && badEntries.Count == 0 && height == 3 &&
                      diffuse + height == rt.Textures.Count,
                      "terrain dumps: the texel dump names the build and, per texture, its role and the md5 of the file received (" + diffuse +
                      " diffuse, " + height + " height" + (badEntries.Count > 0 ? "; wrong: " + string.Join(", ", badEntries.ToArray()) : "") + ")", log);

                // The texel dump streams in bands: T3 (32 x 32) in bands of 3 rows equals the whole read, and every raw file
                // WriteTexelDump writes (bands of 5 rows) holds exactly the floats of the whole read.
                {
                    Texture2D t3 = rt.Textures[MatMdid[2]];
                    int w3, h3;
                    float[] whole3 = WmvTerrainComposite.ReadTexels(t3, 0, out w3, out h3);
                    var banded = new float[whole3.Length];
                    int bands = 0, maxRows = 0;
                    WmvTerrainComposite.StreamTexels(t3, 0, (first, count, rows) =>
                    {
                        bands++;
                        maxRows = Math.Max(maxRows, count);
                        Array.Copy(rows, 0, banded, first * w3 * 4, count * w3 * 4);
                    }, 3);
                    bool sameBands = true;
                    for (int i = 0; i < whole3.Length; i++) sameBands &= banded[i] == whole3[i];
                    string tname = "wmvselftest_texels_" + DateTime.UtcNow.Ticks;
                    int files = 0, sameFiles = 0;
                    try
                    {
                        WmvTerrainComposite.WriteTexelDump(rt, content, cmd, tname, null, 5);
                        foreach (KeyValuePair<uint, Texture2D> kv in rt.Textures)
                        {
                            string p = WmvTerrainComposite.PlayerFile(WmvTerrainComposite.TexelRawName(tname, kv.Key));
                            if (!File.Exists(p)) continue;
                            files++;
                            int tw, th;
                            float[] want = WmvTerrainComposite.ReadTexels(kv.Value, 0, out tw, out th);
                            var wantBytes = new byte[want.Length * 4];
                            Buffer.BlockCopy(want, 0, wantBytes, 0, wantBytes.Length);
                            byte[] got = File.ReadAllBytes(p);
                            bool same = got.Length == wantBytes.Length;
                            for (int i = 0; same && i < got.Length; i++) same = got[i] == wantBytes[i];
                            if (same) sameFiles++;
                        }
                    }
                    finally
                    {
                        foreach (uint id in rt.Textures.Keys)
                        {
                            string p = WmvTerrainComposite.PlayerFile(WmvTerrainComposite.TexelRawName(tname, id));
                            if (File.Exists(p)) File.Delete(p);
                        }
                        if (File.Exists(WmvTerrainComposite.PlayerFile(tname + ".json"))) File.Delete(WmvTerrainComposite.PlayerFile(tname + ".json"));
                    }
                    Check(sameBands && bands == (h3 + 2) / 3 && maxRows == 3 && files == rt.Textures.Count && sameFiles == files && BitConverter.IsLittleEndian,
                          string.Format("terrain dumps: the texel dump is streamed in bands -- a {0}x{1} texture in {2} bands of at most 3 rows " +
                                        "equals the whole read, and the {3} raw file(s) WriteTexelDump wrote in bands of 5 rows hold exactly its floats " +
                                        "({4} equal)", w3, h3, bands, files, sameFiles), log);
                }
            }
            finally
            {
                rt.DisposeImmediate();
            }
        }
        Check(WmvRuntimeTerrainTile.LiveTextures == 0 && WmvRuntimeTerrainTile.LiveAlphaArrays == 0 && TerrainTallies() == "0/0/0",
              "terrain dumps: every tile of the dump checks is released (tallies " + TerrainTallies() + ")", log);
    }

    // ================================================================ states and views

    static void TerrainMaterialViewTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        int meshes0 = Meshes(), materials0 = Materials(), textures0 = Resources.FindObjectsOfTypeAll<Texture2D>().Length,
            arrays0 = Resources.FindObjectsOfTypeAll<Texture2DArray>().Length;
        bool labelsOk = true;
        var labelResults = new List<string>();
        foreach (WmvTerrainView view in new[] { WmvTerrainView.Textured, WmvTerrainView.TexturedLit, WmvTerrainView.TexturedPlainWeights,
                                                WmvTerrainView.Materials, WmvTerrainView.Shaded })
        {
            TerrainTileMesh mesh;
            Dictionary<uint, byte[]> blps;
            WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
            TerrainTileMaterial mat = content.Material;
            var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9100 + (int)view);
            string name = WmvTerrainDebug.ViewNames[(int)view];
            try
            {
                WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = view, OnlyChunk = -1 }, null);
                bool textured = view == WmvTerrainView.Textured || view == WmvTerrainView.TexturedLit || view == WmvTerrainView.TexturedPlainWeights;
                bool plain = view == WmvTerrainView.TexturedPlainWeights;
                float viewValue = view == WmvTerrainView.TexturedLit ? WmvTerrainMaterials.ViewLit
                                : plain ? WmvTerrainMaterials.ViewPlainWeights : WmvTerrainMaterials.ViewTextured;
                int wrong = 0, texturedChunks = 0, neutral = 0, hatched = 0, missingFlags = 0, heightLayers = 0;
                var ids = new HashSet<uint>();
                var stateDrawn = new Dictionary<TerrainMaterialState, string>();
                string first = null;
                for (int n = 0; n < 256; n++)
                {
                    TerrainChunkMaterial c = mat.Chunks[n];
                    Material m = rt.ChunkMaterials[n];
                    if (rt.ChunkObjects[n] == null || m == null)
                    {
                        wrong++;
                        if (first == null) first = n + ": no object";
                        continue;
                    }
                    bool wantTextured = textured && (c.Drawable || (plain && c.PlainDiagnostic));
                    bool ok;
                    if (wantTextured)
                    {
                        texturedChunks++;
                        bool heights = !plain && c.UseHeightTextures;
                        ok = m.shader.name == WmvTerrainMaterials.ShaderName && m.GetFloat(WmvTerrainMaterials.ViewId) == viewValue &&
                             m.GetFloat(WmvTerrainMaterials.HatchId) == (plain ? 1f : 0f) &&
                             m.GetFloat(WmvTerrainMaterials.UseHeightsId) == (heights ? 1f : 0f) &&
                             (int)m.GetFloat(WmvTerrainMaterials.AlphaSliceId) == c.AlphaSlice &&
                             (int)m.GetFloat(WmvTerrainMaterials.AlphaSlice2Id) == Math.Max(0, c.AlphaSlice2) &&
                             (int)m.GetFloat(WmvTerrainMaterials.LayerCountId) == c.Layers.Length && c.Layers.Length <= WmvTerrainMaterials.ShaderLayers &&
                             m.GetFloat(WmvTerrainMaterials.VertexColorsId) == (c.VertexColors != null ? 1f : 0f) && rt.ChunkTextured[n];
                        if (plain) hatched++;
                        for (int k = 0; k < c.Layers.Length; k++)
                        {
                            TerrainMaterialLayer l = c.Layers[k];
                            float miss = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.MissingIds, k);
                            Texture tex = m.GetTexture(WmvTerrainMaterials.LayerIds[k]);
                            if (l.Missing)
                            {
                                ok &= miss == 1f && tex != null && tex == rt.SamplerCarrier;
                                missingFlags++;
                            }
                            else
                            {
                                ok &= miss == 0f && tex != null && tex == rt.Textures[l.TextureFileDataID];
                                ids.Add(l.TextureFileDataID);
                            }
                            float rep = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.RepeatsIds, k);
                            ok &= rep == l.RepeatsPerChunk && WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.PhaseUIds, k) == l.PhaseU &&
                                  WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.PhaseVIds, k) == l.PhaseV;
                            float has = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.HasHeightIds, k);
                            float scale = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.HeightScaleIds, k);
                            float offset = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.HeightOffsetIds, k);
                            bool wantHeight = heights && l.HeightTextureFileDataID != 0;
                            ok &= has == (wantHeight ? 1f : 0f) && scale == (heights ? l.HeightScale : 0f) && offset == (heights ? l.HeightOffset : 1f);
                            if (wantHeight)
                            {
                                heightLayers++;
                                Texture ht = m.GetTexture(WmvTerrainMaterials.HeightIds[k]);
                                ok &= ht != null && ht == rt.Textures[l.HeightTextureFileDataID];
                                ids.Add(l.HeightTextureFileDataID);
                            }
                        }
                        stateDrawn[c.State] = plain ? "plain weights" : "textured";
                    }
                    else if (view == WmvTerrainView.Materials)
                    {
                        ok = m.shader.name == WmvTerrainMaterials.ShaderName && m.GetFloat(WmvTerrainMaterials.ViewId) == WmvTerrainMaterials.ViewStateColour &&
                             m.GetVector(WmvTerrainMaterials.StateColourId) == (Vector4)WmvTerrainMaterials.StateColour(c.State) && rt.ChunkTextured[n];
                        stateDrawn[c.State] = "state colour";
                    }
                    else
                    {
                        neutral++;
                        ok = m == rt.SurfaceMaterial && m.shader.name == WmvTerrainBuilder.ShaderName && !rt.ChunkTextured[n];
                        if (!stateDrawn.ContainsKey(c.State)) stateDrawn[c.State] = "neutral";
                    }
                    if (!ok)
                    {
                        wrong++;
                        if (first == null) first = n + " (" + c.State + "): material " + m.name;
                    }
                }
                int drawable = mat.DrawableChunks;
                int plainChunks = mat.Count(TerrainMaterialState.HeightTextureMissing) + mat.Count(TerrainMaterialState.LayerEffectUnresolved);
                int wantTexturedChunks = textured ? drawable + (plain ? plainChunks : 0) : 0;
                int wantSlices = textured ? mat.DrawableSlices + (plain ? mat.PlainSlices : 0) : 0;
                int states = 0;
                if (view == WmvTerrainView.Materials)
                {
                    var seen = new HashSet<TerrainMaterialState>();
                    foreach (TerrainChunkMaterial c in mat.Chunks) seen.Add(c.State);
                    states = seen.Count;
                }
                // The table of the v3 delta, section 2: what each state is drawn as in this view.
                bool table = true;
                if (textured)
                    foreach (TerrainMaterialState s in Enum.GetValues(typeof(TerrainMaterialState)))
                    {
                        string got;
                        stateDrawn.TryGetValue(s, out got);
                        bool drawnTextured = s == TerrainMaterialState.Textured || s == TerrainMaterialState.MissingTexture ||
                                             s == TerrainMaterialState.LayerEffectStatic ||
                                             (plain && (s == TerrainMaterialState.HeightTextureMissing || s == TerrainMaterialState.LayerEffectUnresolved));
                        table &= got == (drawnTextured ? (plain ? "plain weights" : "textured") : "neutral");
                    }
                int wantMaterials = textured ? wantTexturedChunks + (256 - wantTexturedChunks > 0 ? 1 : 0)
                                  : view == WmvTerrainView.Materials ? states : 1;
                bool counts = texturedChunks == wantTexturedChunks && rt.TexturedMaterialCount == (textured ? wantTexturedChunks : view == WmvTerrainView.Materials ? states : 0) &&
                              rt.MaterialCount == wantMaterials && (rt.AlphaArray != null ? rt.AlphaArray.depth : 0) == wantSlices &&
                              rt.AlphaArrayCount == (wantSlices > 0 ? 1 : 0) && rt.TextureCount == ids.Count && rt.Textures.Count == ids.Count &&
                              WmvRuntimeTerrainTile.LiveTextures == ids.Count && WmvRuntimeTerrainTile.LiveAlphaArrays == rt.AlphaArrayCount &&
                              rt.AlphaGpuBytes == wantSlices * 16384L && WmvRuntimeTerrainTile.LiveTextureBytes == rt.TextureGpuBytes &&
                              (textured ? rt.MaterialVertexBytes > 0 : rt.MaterialVertexBytes == 0) && (!plain || hatched == wantTexturedChunks) &&
                              (!textured || missingFlags == 1) && (view == WmvTerrainView.Textured || view == WmvTerrainView.TexturedLit ? heightLayers == 8 : heightLayers == 0) &&
                              (view != WmvTerrainView.Materials || states == TerrainTileMaterial.StateCount);
                var drawnAs = new List<string>();
                foreach (KeyValuePair<TerrainMaterialState, string> kv in stateDrawn)
                    drawnAs.Add(TerrainTileMaterial.StateName(kv.Key) + " " + kv.Value);
                Check(wrong == 0 && counts && table,
                      string.Format("terrain views: {0} -- {1} chunk(s) by Wmv/TerrainTextured (want {2}{3}), {4} neutral; every material " +
                                    "as specified ({5} wrong{6}); {7} texture(s) ({8} layers reading a height texture), alpha array of {9} slice(s), " +
                                    "{10} material(s), {11} material vertex bytes{12}; drawn: {13}", name, texturedChunks, wantTexturedChunks,
                                    plain ? ", " + hatched + " in plain weights under the hatch" : "", neutral, wrong,
                                    first != null ? "; first " + first : "", rt.TextureCount, heightLayers, rt.AlphaArray != null ? rt.AlphaArray.depth : 0,
                                    rt.MaterialCount, rt.MaterialVertexBytes, view == WmvTerrainView.Materials ? ", " + states + " state colour(s)" : "",
                                    string.Join(", ", drawnAs.ToArray())), log);

                // The status and capture label: from what was drawn (this partial tile draws neutral chunks in every textured
                // view), and VALIDATION sampling named when -wmvTerrainValidate is set.
                string label = WmvMain.WhatTheViewShows(rt, false, () => true), labelV = WmvMain.WhatTheViewShows(rt, true, () => true);
                const string ValidationNote = "; VALIDATION sampling (mip 0 by Load, unfiltered -- not the production look)";
                bool labelOk;
                if (view == WmvTerrainView.Shaded)
                    labelOk = label == WmvMain.GeometryDiagnosticLabel && labelV == label;
                else if (view == WmvTerrainView.Materials)
                    labelOk = label.StartsWith("MATERIAL STATES", StringComparison.Ordinal) && label.Contains(" -- 256 chunk(s) drawn in their state colour") &&
                              !label.Contains("NEUTRAL") && labelV == label;
                else
                    labelOk = label.StartsWith(view == WmvTerrainView.Textured ? "TEXTURE COMPOSITE (" : view == WmvTerrainView.TexturedLit
                                               ? "TEXTURE COMPOSITE under" : "PLAIN-WEIGHTS COMPARISON", StringComparison.Ordinal) &&
                              label.Contains(" -- " + texturedChunks + " chunk(s) drawn textured; NEUTRAL: " + neutral + " of " + (texturedChunks + neutral) +
                                             " chunk(s) drawn in GEOMETRY DIAGNOSTIC shading") &&
                              texturedChunks > 0 && neutral > 0 && !label.Contains("VALIDATION") && labelV == label + ValidationNote;
                labelsOk &= labelOk;
                labelResults.Add(name + (labelOk ? " ok" : " WRONG: \"" + label + "\" / \"" + labelV + "\""));
            }
            finally
            {
                rt.DisposeImmediate();
            }
        }
        // A tile whose material is unavailable, asked for textured: every chunk neutral, no texture, no alpha array.
        {
            TerrainTileMesh mesh = TerrainFromRoot(AdtSynthetic.FlatRoot(tx, ty), tx, ty);
            var content = new WmvTerrainTileContent { Material = TerrainTileMaterial.Unavailable("test: no tex0", 0x84) };
            var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9150);
            try
            {
                WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = -1 }, null);
                Check(rt.MaterialCount == 1 && rt.TextureCount == 0 && rt.AlphaArrayCount == 0 && rt.ChunkMaterials[0] == rt.SurfaceMaterial &&
                      rt.MaterialVertexBytes == 0,
                      "terrain views: a tile without a material asked for textured is drawn neutral (1 material, no texture, no alpha array)", log);
                // Its label is the geometry diagnostic's, with why: no chunk drawable, or (as the player says when the textured
                // shader is missing or unsupported) the shader; never the composite's, never VALIDATION.
                string none = WmvMain.WhatTheViewShows(rt, true, () => true), noShader = WmvMain.WhatTheViewShows(rt, false, () => false);
                string all = "all " + rt.ChunkObjectCount + " chunk(s) shown NEUTRAL";
                bool ok = none.StartsWith(WmvMain.GeometryDiagnosticLabel + " -- the textured view drew no chunk", StringComparison.Ordinal) &&
                          none.Contains("(no chunk of the tile is drawn textured)") && none.EndsWith(all, StringComparison.Ordinal) &&
                          noShader.StartsWith(WmvMain.GeometryDiagnosticLabel, StringComparison.Ordinal) &&
                          noShader.Contains("Resources/" + WmvTerrainMaterials.ShaderResource + ".shader is missing or not supported") &&
                          noShader.EndsWith(all, StringComparison.Ordinal) && rt.ChunkObjectCount > 0;
                labelsOk &= ok;
                labelResults.Add("all neutral " + (ok ? "ok" : "WRONG: \"" + none + "\" / \"" + noShader + "\""));
            }
            finally
            {
                rt.DisposeImmediate();
            }
            // The same tile with -wmvTerrainChunk=0: no chunk of it is drawn textured, hidden or shown -- the whole-tile words.
            var rt0 = new WmvRuntimeTerrainTile(0, tx, ty, 9151);
            try
            {
                WmvTerrainBuilder.Build(rt0, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = 0 }, null);
                string one = WmvMain.WhatTheViewShows(rt0, false, () => true);
                bool ok = one.Contains("(no chunk of the tile is drawn textured)") && one.EndsWith("all 1 chunk(s) shown NEUTRAL", StringComparison.Ordinal);
                labelsOk &= ok;
                labelResults.Add("all neutral, chunk 0 shown " + (ok ? "ok" : "WRONG: \"" + one + "\""));
            }
            finally
            {
                rt0.DisposeImmediate();
            }
        }
        // -wmvTerrainChunk=N on the partial tile: chunk 12 (NoLayers, neutral) shown while the hidden chunks are drawn textured
        // -- the label names the chunk shown, never "no chunk of the tile"; chunk 12 without triangles (every cell a hole) --
        // "has no triangles"; chunk 2 (textured) shown -- the composite's words, one chunk, nothing neutral. (A load's content
        // is built once: each build takes a fresh one.)
        {
            TerrainTileMesh mesh;
            Dictionary<uint, byte[]> blps;
            MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
            byte[] holed = AdtSynthetic.RootWith(tx, ty, n => new RootChunkSpec
            {
                Flags = n == 8 ? 0x10000u : 0x18000u, BaseHeight = 50f, Height = k => (k % 17) * 0.05f,
                Holes = n == 12 ? ulong.MaxValue : n == 6 ? (1UL << 0) | (1UL << 9) | (1UL << 27) | (1UL << 63) : 0UL,
            });
            TerrainTileMesh holedMesh = TerrainFromRoot(holed, tx, ty);
            var onlyChunks = new[] { 12, 12, 2 };
            var meshes = new[] { mesh, holedMesh, mesh };
            var wants = new[] { "(the chunk shown (-wmvTerrainChunk 12) is not drawn textured): all 1 chunk(s) shown NEUTRAL",
                                "(the chunk shown (-wmvTerrainChunk 12) has no triangles): all 0 chunk(s) shown NEUTRAL",
                                "TEXTURE COMPOSITE (" };
            for (int i = 0; i < onlyChunks.Length; i++)
            {
                TerrainTileMesh unused;
                WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out unused, out blps);
                var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9152 + i);
                try
                {
                    WmvTerrainBuilder.Build(rt, meshes[i], content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = onlyChunks[i] },
                                            null);
                    string label = WmvMain.WhatTheViewShows(rt, false, () => true);
                    int hiddenTextured = 0;
                    for (int n = 0; n < 256; n++)
                        if (rt.ChunkObjects[n] != null && !rt.ChunkObjects[n].activeSelf && rt.ChunkTextured[n]) hiddenTextured++;
                    bool ok = onlyChunks[i] == 2
                        ? label.StartsWith(wants[i], StringComparison.Ordinal) && label.EndsWith(" -- 1 chunk(s) drawn textured", StringComparison.Ordinal)
                        : label.StartsWith(WmvMain.GeometryDiagnosticLabel + " -- the textured view drew no chunk", StringComparison.Ordinal) &&
                          label.EndsWith(wants[i], StringComparison.Ordinal) && !label.Contains("no chunk of the tile");
                    ok &= hiddenTextured > 0 && rt.OnlyChunk == onlyChunks[i];
                    labelsOk &= ok;
                    labelResults.Add("chunk " + onlyChunks[i] + " shown" + (i == 1 ? " (no triangles)" : "") + " with " + hiddenTextured +
                                     " hidden chunk(s) drawn textured " + (ok ? "ok" : "WRONG: \"" + label + "\""));
                }
                finally
                {
                    rt.DisposeImmediate();
                }
            }
        }
        Check(labelsOk && labelResults.Count == 10,
              "terrain views: the status and capture label says what was DRAWN -- the counts of chunks drawn textured and NEUTRAL (geometry " +
              "diagnostic shading), the geometry diagnostic's words when no chunk is drawn textured (no chunk drawable, the chunk " +
              "-wmvTerrainChunk shows not drawn textured or without triangles while hidden chunks are, or the textured shader missing), " +
              "VALIDATION sampling when -wmvTerrainValidate is set: " + string.Join("; ", labelResults.ToArray()), log);
        // A height texture whose upload fails: its chunks become HeightTextureMissing at build time and are drawn neutral.
        {
            TerrainTileMesh mesh;
            Dictionary<uint, byte[]> blps;
            WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
            // A prepared texture whose upload throws: the prepared size says 0 x 0 (the constructor refuses it).
            content.Textures[992006].Width = 0;
            var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9155);
            try
            {
                WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = -1 }, null);
                TerrainTileMaterial mat = content.Material;
                bool neutral10 = rt.ChunkMaterials[10] == rt.SurfaceMaterial && rt.ChunkMaterials[17] == rt.SurfaceMaterial &&
                                 rt.ChunkMaterials[18] == rt.SurfaceMaterial;
                Check(content.Textures[992006].State == WmvTerrainTexture.StateDecodeFailed && mat.Chunks[10].State == TerrainMaterialState.HeightTextureMissing &&
                      mat.Chunks[17].State == TerrainMaterialState.HeightTextureMissing && mat.Chunks[18].State == TerrainMaterialState.HeightTextureMissing &&
                      neutral10 && !rt.Textures.ContainsKey(992006) && rt.ChunkTextured[14] && mat.Chunks[14].State == TerrainMaterialState.Textured,
                      "terrain views: a height texture whose upload fails (\"" + content.Textures[992006].Detail + "\") makes its chunks 10, 17 and 18 " +
                      "heightTextureMissing, drawn neutral; chunk 14 (other heights) stays textured", log);
                // What stays on the tile is exactly what the materials of the chunks still drawn textured bind: T6 (991006), uploaded
                // for chunk 10 before its height texture failed, is used only by chunks 10, 17 and 18, all heightTextureMissing now
                // -- released, its upload time reset.
                var bound = new HashSet<Texture>();
                long boundBytes = 0;
                for (int n = 0; n < 256; n++)
                {
                    Material m = rt.ChunkMaterials[n];
                    if (!rt.ChunkTextured[n] || m == null)
                        continue;
                    int layers = (int)m.GetFloat(WmvTerrainMaterials.LayerCountId);
                    for (int k = 0; k < layers; k++)
                    {
                        float miss = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.MissingIds, k);
                        float has = WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.HasHeightIds, k);
                        if (miss == 0f) bound.Add(m.GetTexture(WmvTerrainMaterials.LayerIds[k]));
                        if (has == 1f) bound.Add(m.GetTexture(WmvTerrainMaterials.HeightIds[k]));
                    }
                }
                bound.Remove(null);
                bool allBound = true;
                foreach (KeyValuePair<uint, Texture2D> kv in rt.Textures)
                {
                    allBound &= bound.Contains(kv.Value);
                    boundBytes += content.Textures[kv.Key].GpuBytes;
                }
                WmvTerrainTexture t6 = content.Textures[991006];
                Check(!rt.Textures.ContainsKey(991006) && t6.Available && t6.UploadMs < 0 && allBound && rt.Textures.Count == bound.Count &&
                      rt.TextureCount == bound.Count && WmvRuntimeTerrainTile.LiveTextures == bound.Count && rt.TextureGpuBytes == boundBytes &&
                      WmvRuntimeTerrainTile.LiveTextureBytes == rt.TextureGpuBytes && rt.AlphaArrayCount == 1,
                      string.Format("terrain views: after that upload failure the tile keeps only the textures its remaining materials bind -- {0} " +
                                    "bound, {1} on the tile ({2} tracked, {3} live, {4} GPU bytes = {5} bound bytes, live {6}); 991006 (T6, uploaded for " +
                                    "chunk 10, used only by chunks 10, 17 and 18) released, \"{7}\" with no uploadMs ({8})", bound.Count, rt.Textures.Count, rt.TextureCount,
                                    WmvRuntimeTerrainTile.LiveTextures, rt.TextureGpuBytes, boundBytes, WmvRuntimeTerrainTile.LiveTextureBytes, t6.State,
                                    t6.UploadMs), log);
            }
            finally
            {
                rt.DisposeImmediate();
            }
        }
        // A layer-effect-static chunk whose diffuse texture fails keeps its state (the contract's precedence) and draws the
        // checker: the settle log counts it among the chunks drawing the checker in the failed texture's place.
        {
            TerrainTileMesh mesh;
            Dictionary<uint, byte[]> blps;
            WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
            content.Textures[MatMdid[7]].Fail("not installed locally", "test: T7, the static layer's texture");
            List<uint> failedDiffuse, failedHeight;
            content.SettleFailures(out failedDiffuse, out failedHeight);
            int inPlace, staticInPlace, all;
            content.CheckerChunks(failedDiffuse, out inPlace, out staticInPlace, out all);
            TerrainTileMaterial mat = content.Material;
            Check(failedDiffuse.Count == 1 && failedDiffuse[0] == MatMdid[7] && mat.Chunks[11].State == TerrainMaterialState.LayerEffectStatic &&
                  mat.Chunks[11].Layers[1].Missing && inPlace == 1 && staticInPlace == 1 && all == 2 && mat.Count(TerrainMaterialState.MissingTexture) == 1,
                  string.Format("terrain views: T7 failing leaves chunk 11 layerEffectStatic (precedence) with its layer drawn as the checker; the " +
                                "settle count says {0} chunk(s) draw it in the failed texture's place ({1} layerEffectStatic), {2} draw it in all (chunk 5's " +
                                "MDID 0 too) -- where the missingTexture count alone is {3}", inPlace, staticInPlace, all,
                                mat.Count(TerrainMaterialState.MissingTexture)), log);
        }
        // A layer of unresolved repeats (RepeatsPerChunk 0: an MTXP scale nibble above 4; LayerEffectUnresolved, drawn only in
        // the plain-weights diagnostic) is the checker, its texture not bound (the slot holds the sampler carrier).
        {
            var c = new TerrainChunkMaterial
            {
                Index = 0, State = TerrainMaterialState.LayerEffectUnresolved, AlphaSlice = 0,
                Layers = new[]
                {
                    new TerrainMaterialLayer { IndexInRange = true, TextureFileDataID = MatMdid[0], RepeatsPerChunk = 0, ScaleExponent = 5 },
                    new TerrainMaterialLayer { IndexInRange = true, TextureFileDataID = MatMdid[1], RepeatsPerChunk = 4, ScaleExponent = 1 },
                },
            };
            var tex = new Texture2D(4, 4, TextureFormat.RGBA32, false, true);
            Texture2D carrier = null;
            Material m = null;
            try
            {
                WmvTerrainMaterials.CreateSamplerCarrier(x => carrier = x);
                m = WmvTerrainMaterials.ChunkMaterial(WmvTerrainMaterials.ResolveShader(null), c, id => tex, null, carrier, WmvTerrainMaterials.ViewPlainWeights,
                                                      true);
                Check(WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.MissingIds, 0) == 1f &&
                      WmvTerrainMaterials.LayerValue(m, WmvTerrainMaterials.MissingIds, 1) == 0f && m.GetTexture(WmvTerrainMaterials.LayerIds[0]) == carrier &&
                      m.GetTexture(WmvTerrainMaterials.LayerIds[1]) == tex,
                      "terrain views: a layer of unresolved repeats (MTXP scale nibble 5: repeats 0) is drawn as the checker in the plain-weights " +
                      "diagnostic, its texture not bound (its slot holds the sampler carrier); the next layer (4 repeats) bound as usual", log);
            }
            finally
            {
                if (m != null) UnityEngine.Object.DestroyImmediate(m);
                if (carrier != null) UnityEngine.Object.DestroyImmediate(carrier);
                UnityEngine.Object.DestroyImmediate(tex);
            }
        }
        // The player's own deferred disposal takes the textures and the array off the tallies at once.
        {
            TerrainTileMesh mesh;
            Dictionary<uint, byte[]> blps;
            WmvTerrainTileContent content = MaterialTestContent(tx, ty, MatHeightsOn, out mesh, out blps);
            var rt = new WmvRuntimeTerrainTile(0, tx, ty, 9160);
            WmvTerrainBuilder.Build(rt, mesh, content, null, new WmvTerrainBuildOptions { View = WmvTerrainView.Textured, OnlyChunk = -1 }, null);
            int texturesHeld = WmvRuntimeTerrainTile.LiveTextures;
            List<UnityEngine.Object> owned = rt.OwnedObjects();
            rt.Dispose();
            bool atOnce = WmvRuntimeTerrainTile.LiveTextures == 0 && WmvRuntimeTerrainTile.LiveTextureBytes == 0 && WmvRuntimeTerrainTile.LiveAlphaArrays == 0 &&
                          TerrainTallies() == "0/0/0";
            foreach (UnityEngine.Object o in owned) if (o != null) UnityEngine.Object.DestroyImmediate(o);
            Check(texturesHeld > 0 && atOnce, "terrain views: the player's deferred Dispose takes the tile's " + texturesHeld + " texture(s) and its " +
                  "alpha array off the tallies at once", log);
        }
        Check(Meshes() == meshes0 && Materials() == materials0 && Resources.FindObjectsOfTypeAll<Texture2D>().Length == textures0 &&
              Resources.FindObjectsOfTypeAll<Texture2DArray>().Length == arrays0,
              "terrain views: every Mesh, Material, Texture2D and Texture2DArray of the view checks is gone", log);
    }

    // ================================================================ the session with textures

    const int MatWdt = 990700, MatRoot = 990701, MatTex0 = 990702, MatMapTexture = 990703;

    /// <summary>A WDT (MPHD flags) whose MAID names root, tex0 and map texture for one tile.</summary>
    static byte[] TexturedWdt(int tx, int ty, uint mphd, uint root, uint tex0, uint mapTexture)
    {
        var maid = new byte[4096 * 32];
        int idx = ty * 64 + tx;
        AdtSynthetic.PutU32(maid, idx * 32, root);
        AdtSynthetic.PutU32(maid, idx * 32 + 12, tex0);
        AdtSynthetic.PutU32(maid, idx * 32 + 20, mapTexture);
        return AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(mphd), AdtSynthetic.MainTable(idx), AdtSynthetic.Chunk("MAID", maid));
    }

    static WmvIpcClient.TerrainTileCommand TexturedCommand(int load, int tx, int ty, int maxTextures = 128)
    {
        WmvIpcClient.TerrainTileCommand c = TerrainCommand(load, tx, ty, 4194304, MatWdt);
        c.MaxTextureBytes = 8 * 1024 * 1024;
        c.MaxTextureBytesPerLoad = 128 * 1024 * 1024;
        c.MaxTexturesPerLoad = maxTextures;
        return c;
    }

    /// <summary>The test host's files: the WDT (MPHD `mphd`), the root, the tex0 and the textures, by FileDataID (the height
    /// texture 992011 is in no client's storage).</summary>
    sealed class MaterialHost
    {
        public readonly Dictionary<int, byte[]> Files = new Dictionary<int, byte[]>();

        public MaterialHost(int tx, int ty, byte[] tex0 = null, Dictionary<uint, byte[]> textures = null, uint mphd = MatHeightsOn)
        {
            Files[MatWdt] = TexturedWdt(tx, ty, mphd, MatRoot, MatTex0, MatMapTexture);
            Files[MatRoot] = MaterialTestRoot(tx, ty);
            Files[MatTex0] = tex0 ?? MaterialTestTex0();
            foreach (KeyValuePair<uint, byte[]> kv in textures ?? MaterialTestTextures())
                Files[(int)kv.Key] = kv.Value;
        }

        /// <summary>Answer request `id` from the files ("available"); false when the session did not apply it.</summary>
        public bool Serve(TerrainHarness h, string id)
        {
            string[] req = h.Sent.Find(s => s[0] == id);
            byte[] b;
            return Files.TryGetValue(int.Parse(req[2]), out b) ? h.Answer(id, b) : h.Answer(id, null, "not in client storage", "test: no such file");
        }

        /// <summary>Answer every request the session waits for, then a frame, until nothing more is asked (at most `rounds`).</summary>
        public void ServeAll(TerrainHarness h, int rounds = 40)
        {
            for (int i = 0; i < rounds; i++)
            {
                List<string> ids = h.Session.PendingIds();
                foreach (string id in ids) Serve(h, id);
                h.Pump(1);
                if (ids.Count == 0 && h.Session.PendingRequests == 0 && h.Session.PreparingTextures == 0)
                    break;
            }
        }
    }

    static int TexturesSent(TerrainHarness h)
    {
        int n = 0;
        foreach (string[] s in h.Sent) if (s[1] == WmvTerrainSession.RoleTexture) n++;
        return n;
    }

    static bool SentTexture(TerrainHarness h, uint id)
    {
        foreach (string[] s in h.Sent) if (s[1] == WmvTerrainSession.RoleTexture && s[2] == id.ToString()) return true;
        return false;
    }

    static WmvIpcClient.TerrainTextureReport TextureEntry(WmvIpcClient.TerrainTileReport r, int id, string role = null)
    {
        if (r == null || r.Material == null) return null;
        return r.Material.Textures.Find(t => t.FileDataID == id && (role == null || t.Role == role));
    }

    static int ChunkState(WmvIpcClient.TerrainTileReport r, string name)
    {
        if (r == null || r.Material == null) return -1;
        foreach (KeyValuePair<string, int> kv in r.Material.ChunkStates) if (kv.Key == name) return kv.Value;
        return -1;
    }

    /// <summary>The synthetic tile's material as a load settles it with the given texture failures (diffuse, height).</summary>
    static TerrainTileMaterial SettledMaterial(int tx, int ty, uint mphd, uint[] failedDiffuse, uint[] failedHeight)
    {
        TerrainTileMaterial m = TerrainMaterialBuilder.Build(AdtParser.ParseRoot(MaterialTestRoot(tx, ty), tx, ty), AdtParser.ParseTex(MaterialTestTex0()), mphd);
        m.ApplyTextureFailures(failedDiffuse);
        m.ApplyHeightTextureFailures(failedHeight);
        return m;
    }

    /// <summary>The textures a load uploads for the textured view: the non-missing diffuse ids of the drawable chunks and,
    /// with MPHD 0x80, their height ids.</summary>
    static int UploadedFor(TerrainTileMaterial m)
    {
        var ids = new HashSet<uint>();
        foreach (TerrainChunkMaterial c in m.Chunks)
        {
            if (!c.Drawable) continue;
            foreach (TerrainMaterialLayer l in c.Layers)
            {
                if (!l.Missing && l.TextureFileDataID != 0) ids.Add(l.TextureFileDataID);
                if (c.UseHeightTextures && l.HeightTextureFileDataID != 0) ids.Add(l.HeightTextureFileDataID);
            }
        }
        return ids.Count;
    }

    static void TerrainTextureSessionTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        int live0 = WmvRuntimeTerrainTile.Live;
        var harnesses = new List<TerrainHarness>();
        string rw = WmvTerrainSession.RoleWdt, rr = WmvTerrainSession.RoleRootAdt, rt0 = WmvTerrainSession.RoleTex0Adt,
               rtex = WmvTerrainSession.RoleTexture;
        // The textures of the drawable chunks: 10 diffuse (T1..T9, T11; T10 is used only by the LayerEffectUnresolved chunk)
        // and, with MPHD 0x80, 4 height textures (992011 among them, which no host has).
        TerrainTileMaterial built = SettledMaterial(tx, ty, MatHeightsOn, null, null);
        int diffuseIds = built.TextureIds.Length, heightIds = built.HeightTextureIds.Length, unique = diffuseIds + heightIds;
        Check(diffuseIds == 10 && heightIds == 4 && built.PlainOnlyTextureIds.Length == 1,
              "terrain textured load: (precondition) the drawable chunks of the synthetic tile use " + diffuseIds + " diffuse and " + heightIds +
              " height textures (MPHD 0x80), the plain-diagnostic chunk " + built.PlainOnlyTextureIds.Length + " more", log);

        // ---- a textured load: the WDT alone, then the root and the tex0 together, then the textures (diffuse first, then
        // the height textures), never more than K out; the height texture no host has makes its chunk HeightTextureMissing
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(601, tx, ty));
            bool wdtAlone = h.Sent.Count == 1 && h.Sent[0][1] == rw && h.Session.PendingRequests == 1;
            host.Serve(h, h.Last(rw));
            bool rootAndTex0 = h.Sent.Count == 3 && h.Sent[1][1] == rr && h.Sent[2][1] == rt0 && h.Sent[1][2] == MatRoot.ToString() &&
                               h.Sent[2][2] == MatTex0.ToString() && h.Session.PendingRequests == 2;
            host.Serve(h, h.Last(rr));
            host.Serve(h, h.Last(rt0));
            h.Pump(1);                                                       // root mesh, tex0 parse, the material: textures asked
            int firstWave = TexturesSent(h);
            List<string> wave = h.Session.PendingIds();
            foreach (string id in wave) host.Serve(h, id);                   // answered: being prepared, still counted against K
            bool heldByPreparation = TexturesSent(h) == firstWave && h.Session.PendingRequests == 0 &&
                                     h.Session.PreparingTextures == wave.Count && h.Session.Outstanding == wave.Count;
            h.Pump(1);                                                       // prepared: the next ones asked
            int secondWave = TexturesSent(h) - firstWave;
            host.ServeAll(h);
            h.Pump(3);
            // The order asked: every diffuse id before any height id.
            var askedOrder = new List<uint>();
            foreach (string[] s in h.Sent) if (s[1] == rtex) askedOrder.Add(uint.Parse(s[2]));
            bool diffuseFirst = askedOrder.Count == unique;
            for (int i = 0; diffuseFirst && i < askedOrder.Count; i++)
                diffuseFirst &= (i < diffuseIds) == (Array.IndexOf(built.TextureIds, askedOrder[i]) >= 0);
            WmvIpcClient.TerrainTileReport r = h.For(601);
            TerrainTileMaterial want = SettledMaterial(tx, ty, MatHeightsOn, null, new[] { MatMissingHeight });
            int heightEntries = 0, diffuseEntries = 0;
            string badEntry = null;
            bool entries = r != null && r.Material != null && r.Material.Textures.Count == diffuseIds + 1 + heightIds;
            if (entries)
                foreach (WmvIpcClient.TerrainTextureReport t in r.Material.Textures)
                {
                    if (t.Role == "height") heightEntries++; else if (t.Role == "diffuse") diffuseEntries++;
                    bool isHeight = Array.IndexOf(built.HeightTextureIds, (uint)t.FileDataID) >= 0;
                    // T11 (991011) is fetched for chunk 15, which turns HeightTextureMissing once 992011 fails: never uploaded.
                    bool uploaded = t.FileDataID != 991011;
                    bool ok = t.Role == (isHeight ? "height" : "diffuse") &&
                              (t.FileDataID == 0 ? t.State == WmvTerrainTexture.StateNoTextureId
                               : t.FileDataID == (int)MatMissingHeight ? t.State == "not in client storage" && t.Detail == "test: no such file"
                               : t.State == "available" && t.Width > 0 && t.Mips > 0 && (uploaded ? t.UploadMs >= 0 : t.UploadMs < 0) &&
                                 t.Format.Length > 0);
                    if (!ok && badEntry == null)
                        badEntry = string.Format("{0} {1} {2} upload {3} format {4}", t.FileDataID, t.Role, t.State, t.UploadMs, t.Format);
                    entries &= ok;
                }
            Check(wdtAlone && rootAndTex0 && firstWave == WmvTerrainSession.MaxOutstanding && heldByPreparation &&
                  secondWave == Math.Min(WmvTerrainSession.MaxOutstanding, unique - firstWave) && diffuseFirst &&
                  h.Session.OutstandingPeak == WmvTerrainSession.MaxOutstanding && TexturesSent(h) == unique,
                  string.Format("terrain textured load: the WDT alone, then the root and the tex0 together, then textures: {0} asked at once (K), " +
                                "the {1} answered held K while prepared (none more asked), {2} asked once they were; {3} distinct textures asked " +
                                "(the {4} diffuse ones first, then the {5} height ones; the plain-only one not); never more than {6} out", firstWave,
                                wave.Count, secondWave, TexturesSent(h), diffuseIds, heightIds, h.Session.OutstandingPeak), log);
            Check(r != null && r.Status == "built" && r.Material != null && r.Material.State == "partial" && entries && heightEntries == heightIds &&
                  diffuseEntries == diffuseIds + 1 && r.Material.UniqueTextures == diffuseIds &&
                  ChunkState(r, "textured") == want.Count(TerrainMaterialState.Textured) && ChunkState(r, "textured") == 249 &&
                  ChunkState(r, "heightTextureMissing") == 1 && ChunkState(r, "layerEffectStatic") == 1 && ChunkState(r, "layerEffectUnresolved") == 1 &&
                  ChunkState(r, "missingTexture") == 1 && ChunkState(r, "malformed") == 1 && ChunkState(r, "noLayers") == 1 &&
                  r.Material.Tex0FileDataID == MatTex0 && r.Material.Tex0Bytes == host.Files[MatTex0].Length &&
                  r.Material.Tex0Md5 == WmvTerrainSession.Md5Hex(host.Files[MatTex0]) && r.Material.AlphaFormat == "8-bit" &&
                  r.Material.TextureGpuBytes > 0 && r.Material.AlphaGpuBytes == (long)want.DrawableSlices * 16384 &&
                  r.Material.Materials == want.DrawableChunks && r.Material.MaterialVertexBytes > 0 && r.RequestsRetired == 0 &&
                  r.Material.Tex0RoundTripMs >= 0 && r.Material.MaterialBuildMs >= 0 && r.Material.TextureUploadMs >= 0 &&
                  WmvRuntimeTerrainTile.LiveTextures == UploadedFor(want) && WmvRuntimeTerrainTile.LiveAlphaArrays == 1 && h.View.LastView == WmvTerrainView.Textured,
                  string.Format("terrain textured load: built with material {0} ({1}); {2} texture entries ({3} diffuse with \"no texture id\", {4} " +
                                "height, each under its role; 992011 \"not in client storage\"; 991011 fetched, not uploaded{16}); chunks " +
                                "textured {5}, heightTextureMissing {6}, " +
                                "layerEffectStatic {7}; GPU texture bytes {8}, alpha {9}, {10} materials, {11} material vertex bytes; tallies {12} " +
                                "texture(s) (want {13}), {14} alpha array(s); drawn {15} by default", r != null && r.Material != null ? r.Material.State : "none",
                                r != null && r.Material != null ? r.Material.Reason : "", r != null && r.Material != null ? r.Material.Textures.Count : -1,
                                diffuseEntries, heightEntries, ChunkState(r, "textured"), ChunkState(r, "heightTextureMissing"),
                                ChunkState(r, "layerEffectStatic"), r != null && r.Material != null ? r.Material.TextureGpuBytes : -1,
                                r != null && r.Material != null ? r.Material.AlphaGpuBytes : -1, r != null && r.Material != null ? r.Material.Materials : -1,
                                r != null && r.Material != null ? r.Material.MaterialVertexBytes : -1, WmvRuntimeTerrainTile.LiveTextures,
                                UploadedFor(want), WmvRuntimeTerrainTile.LiveAlphaArrays, h.View.LastView,
                                badEntry != null ? "; WRONG ENTRY " + badEntry : ""), log);
            h.Session.Clear("test");
            Check(WmvRuntimeTerrainTile.LiveTextures == 0 && WmvRuntimeTerrainTile.LiveAlphaArrays == 0 && TerrainTallies() == "0/0/0",
                  "terrain textured load: clearTerrain releases its textures and alpha array (tallies 0)", log);
        }

        // ---- a map without MPHD 0x80: no height texture asked, none reported, the chunk whose height is missing Textured
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty, null, null, MatHeightsOff);
            h.Session.Load(TexturedCommand(611, tx, ty));
            host.ServeAll(h);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(611);
            bool noHeight = true;
            foreach (uint hid in new[] { 992006u, 992008u, 992009u, MatMissingHeight }) noHeight &= !SentTexture(h, hid);
            Check(r != null && r.Status == "built" && noHeight && TexturesSent(h) == diffuseIds && r.Material != null &&
                  r.Material.Textures.TrueForAll(t => t.Role == "diffuse") && ChunkState(r, "heightTextureMissing") == 0 &&
                  ChunkState(r, "textured") == 250,
                  "terrain textured load without MPHD 0x80: " + TexturesSent(h) + " diffuse textures asked, no height texture (MHID ignored), every " +
                  "entry \"diffuse\", chunks textured " + ChunkState(r, "textured") + ", heightTextureMissing " + ChunkState(r, "heightTextureMissing"), log);
            h.Session.Clear("test");
        }

        // ---- the plain-weights view: the plain-only texture is asked too
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            h.View.View = WmvTerrainView.TexturedPlainWeights;
            h.View.PlainDiagnostic = true;
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(612, tx, ty));
            host.ServeAll(h);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(612);
            uint plainOnly = built.PlainOnlyTextureIds[0];
            Check(r != null && r.Status == "built" && SentTexture(h, plainOnly) && TexturesSent(h) == unique + 1 &&
                  TextureEntry(r, (int)plainOnly, "diffuse") != null && h.View.LastView == WmvTerrainView.TexturedPlainWeights,
                  "terrain textured load in texturedplainweights: the plain-diagnostic chunk's texture " + plainOnly + " is asked too (" +
                  TexturesSent(h) + " textures)", log);
            h.Session.Clear("test");
        }

        // ---- every texture state: the host's quoted, and the player's own; the chunks using a failed diffuse texture
        // MissingTexture, those reading a failed height texture HeightTextureMissing
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            host.Files[991002] = new byte[] { 1, 2, 3 };                               // not a BLP: decode failed
            h.Session.Load(TexturedCommand(602, tx, ty));
            for (int round = 0; round < 40 && h.Reports.Count == 0; round++)
            {
                foreach (string id in h.Session.PendingIds())
                {
                    string[] req = h.Sent.Find(s => s[0] == id);
                    int fd = int.Parse(req[2]);
                    if (fd == 991001) h.Answer(id, null, "not installed locally", "listed, not on this computer");
                    else if (fd == 991003 || fd == 992006) h.Answer(id, host.Files[fd], "available", "", "00000000000000000000000000000000");
                    else if (fd == 991004)
                        h.Session.ClaimMapAssetResponse(new WmvIpcClient.AssetResponse
                        {
                            requestId = id, ok = false, role = rtex, load = 602, state = "", error = "the connection closed", detail = "",
                            md5 = "", path = "",
                        });
                    else host.Serve(h, id);
                }
                h.Pump(1);
            }
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(602);
            WmvIpcClient.TerrainTextureReport nil = TextureEntry(r, 991001), dec = TextureEntry(r, 991002), md5 = TextureEntry(r, 991003),
                                              tr = TextureEntry(r, 991004), ok = TextureEntry(r, 991005), zero = TextureEntry(r, 0),
                                              hmd5 = TextureEntry(r, 992006, "height");
            bool states = nil != null && nil.State == "not installed locally" && nil.Detail == "listed, not on this computer" &&
                          dec != null && dec.State == WmvTerrainTexture.StateDecodeFailed && md5 != null && md5.State == WmvTerrainTexture.StateMd5Mismatch &&
                          tr != null && tr.State == WmvTerrainTexture.StateTransportFailure && tr.Detail == "the connection closed" &&
                          ok != null && ok.State == "available" && zero != null && zero.State == WmvTerrainTexture.StateNoTextureId &&
                          hmd5 != null && hmd5.State == WmvTerrainTexture.StateMd5Mismatch;
            TerrainTileMaterial want = SettledMaterial(tx, ty, MatHeightsOn, new uint[] { 991001, 991002, 991003, 991004 }, new[] { 992006u, MatMissingHeight });
            Check(r != null && r.Status == "built" && states && r.Material.State == "partial" &&
                  ChunkState(r, "textured") == want.Count(TerrainMaterialState.Textured) && ChunkState(r, "missingTexture") == want.Count(TerrainMaterialState.MissingTexture) &&
                  ChunkState(r, "heightTextureMissing") == 4 && want.Count(TerrainMaterialState.HeightTextureMissing) == 4,
                  "terrain texture states: \"not installed locally\" (the host's state and detail quoted), \"decode failed\" (" +
                  (dec != null ? dec.Detail : "none") + "), \"md5 mismatch\" (a diffuse and a height texture), \"transport failure\", \"available\", " +
                  "\"no texture id\" (MDID 0); chunks now textured " + ChunkState(r, "textured") + ", missingTexture " + ChunkState(r, "missingTexture") +
                  ", heightTextureMissing " + ChunkState(r, "heightTextureMissing") + " (chunks 10, 15, 17, 18)", log);
            h.Session.Clear("test");
        }

        // ---- the per-load cap: maxTexturesPerLoad 2 -> 2 asked, the rest "not requested" (the height textures too: their
        // chunks HeightTextureMissing)
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(603, tx, ty, 2));
            host.ServeAll(h);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(603);
            int notRequested = r != null && r.Material != null ? r.Material.Textures.FindAll(t => t.State == WmvTerrainTexture.StateNotRequested).Count : -1;
            int requestState = r != null && r.Material != null
                ? r.Material.Textures.FindAll(t => t.State != WmvTerrainTexture.StateNotRequested && t.State != WmvTerrainTexture.StateNoTextureId).Count : -1;
            Check(r != null && r.Status == "built" && TexturesSent(h) == 2 && notRequested == unique - 2 && requestState == 2 &&
                  r.Material.State == "partial" && ChunkState(r, "heightTextureMissing") == 5,
                  "terrain texture cap: maxTexturesPerLoad 2 asks for 2 and reports " + notRequested + " \"not requested\" (2 entries in a request " +
                  "state); the 5 chunks reading height textures (10, 14, 15, 17, 18) heightTextureMissing (" + ChunkState(r, "heightTextureMissing") + ")", log);
            h.Session.Clear("test");
        }

        // ---- a tex0 that fails: the tile built neutral, its material "unavailable" with the reason; nothing more asked
        Action<string, Action<TerrainHarness, MaterialHost>, Func<MaterialHost, string>> tex0Fails = (what, answer, reason) =>
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(604, tx, ty));
            host.Serve(h, h.Last(rw));
            host.Serve(h, h.Last(rr));
            answer(h, host);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(604);
            string want = reason(host);
            Check(r != null && r.Status == "built" && r.Material != null && r.Material.State == "unavailable" && r.Material.Reason == want &&
                  TexturesSent(h) == 0 && r.HostState == "" && h.View.LastView == WmvTerrainView.Shaded && WmvRuntimeTerrainTile.LiveTextures == 0,
                  "terrain tex0 failure: " + what + " -> the tile built neutral (shaded), material \"unavailable\", reason exactly \"" + want + "\" (" +
                  (r != null && r.Material != null ? r.Material.Reason : "no material") + "), no texture asked", log);
            h.Session.Clear("test");
        };
        tex0Fails("the host refuses it", (h, host) => h.Answer(h.Last(rt0), null, "unreadable", "not a tex0 ADT: second chunk RDHM (hex 52 44 48 4d)"),
                  host => "the host answered the tex0 ADT request (FileDataID " + MatTex0 + ") \"unreadable\": not a tex0 ADT: second chunk RDHM (hex 52 44 48 4d)");
        tex0Fails("its bytes differ from the host's md5", (h, host) => h.Answer(h.Last(rt0), host.Files[MatTex0], "available", "", "00000000000000000000000000000000"),
                  host => "the tex0 ADT bytes received hash to " + WmvTerrainSession.Md5Hex(host.Files[MatTex0]) + ", the host served 00000000000000000000000000000000");
        byte[] junkTex0 = AdtSynthetic.Cat(AdtSynthetic.Mver());
        tex0Fails("it does not parse", (h, host) => h.Answer(h.Last(rt0), junkTex0),
                  host => "tex0 ADT parse failed: " + ThrownMessage(() => AdtParser.ParseTex(junkTex0, "tex0 ADT " + MatTex0 + " (" + tx + "_" + ty + ")")));

        // ---- a protocol-12 host (no maxTextureBytes): no tex0 asked, material "unavailable"
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TerrainCommand(605, tx, ty, 4194304, MatWdt));
            host.ServeAll(h);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(605);
            Check(r != null && r.Status == "built" && h.Sent.Count == 2 && h.Sent.TrueForAll(s => s[1] != rt0 && s[1] != rtex) && r.Material != null &&
                  r.Material.State == "unavailable" && r.Material.Reason == WmvTerrainSession.NoTexturesOffered && r.RequestsRetired == 0,
                  "terrain protocol 12 host: no tex0 or texture asked (" + h.Sent.Count + " requests), material \"unavailable\" (\"" +
                  (r != null && r.Material != null ? r.Material.Reason : "none") + "\")", log);
            h.Session.Clear("test");
        }

        // ---- K and retirement: a load superseded with 4 textures out retires all 4 and names them; their answers are late
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(606, tx, ty));
            host.Serve(h, h.Last(rw));
            host.Serve(h, h.Last(rr));
            host.Serve(h, h.Last(rt0));
            h.Pump(1);
            List<string> out4 = h.Session.PendingIds();
            int before = h.Session.RetiredTotal, late0 = h.Session.LateAnswersDropped, sent0 = h.Sent.Count;
            h.Session.Load(TexturedCommand(607, tx, ty));
            WmvIpcClient.TerrainTileReport r = h.For(606);
            bool named = r != null && r.RequestsRetired == 4 && r.RetiredRequestIds.Count == 4;
            for (int i = 0; named && i < 4; i++) named &= r.RetiredRequestIds[i] == out4[i];
            bool inRing = true;
            foreach (string id in out4)
            {
                WmvTerrainSession.RetiredRequest e;
                inRing &= h.Session.TryGetRetired(id, out e) && e.Load == 606 && e.Role == rtex && e.Ended == "superseded";
            }
            int applied = 0;
            foreach (string id in out4) if (host.Serve(h, id)) applied++;
            h.Pump(2);
            Check(out4.Count == 4 && r != null && r.Status == "superseded" && named && inRing && h.Session.RetiredTotal == before + 4 &&
                  h.Session.LastRetiredRequest == out4[3] && applied == 0 && h.Session.LateAnswersDropped == late0 + 4 &&
                  h.Sent.Count == sent0 + 1 && h.Session.LoadInFlight == 607 && h.Session.PendingRequests == 1,
                  "terrain K: a load superseded with 4 textures out retires all 4 (requestsRetired " + (r != null ? r.RequestsRetired : -1) +
                  ", the ids named in order, the ring holding each, the total +" + (h.Session.RetiredTotal - before) + ", the last " +
                  h.Session.LastRetiredRequest + "); their 4 late answers dropped, nothing asked for them -- the newer load waits for its WDT", log);
            h.Session.Clear("test");
        }

        // ---- textures being prepared when the load ends: not requests (nothing retired for them), their results dropped
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(608, tx, ty));
            host.Serve(h, h.Last(rw));
            host.Serve(h, h.Last(rr));
            host.Serve(h, h.Last(rt0));
            h.Pump(1);
            List<string> ids = h.Session.PendingIds();
            host.Serve(h, ids[0]);
            host.Serve(h, ids[1]);
            int preparing = h.Session.PreparingTextures, pending = h.Session.PendingRequests;
            h.Session.Clear("the client was replaced");                                 // a client switch mid-texture
            WmvIpcClient.TerrainTileReport r = h.For(608);
            int late0 = h.Session.LateAnswersDropped;
            bool lateStale = !h.Answer(ids[2], null, "stale", "request epoch 7, current 8") && !h.Answer(ids[3], null, "stale", "request epoch 7, current 8");
            h.Pump(2);
            int requested = r != null && r.Material != null ? r.Material.Textures.FindAll(t => t.State == WmvTerrainTexture.StateRequested).Count : -1;
            int queued = r != null && r.Material != null ? r.Material.Textures.FindAll(t => t.State == WmvTerrainTexture.StateQueued).Count : -1;
            string unsettled = "the load ended before its textures settled: " + unique + " texture(s) unanswered; chunk states count the answered failures only";
            Check(preparing == 2 && pending == 2 && r != null && r.Status == "cleared" && r.RequestsRetired == 2 && r.RetiredRequestIds[0] == ids[2] &&
                  r.RetiredRequestIds[1] == ids[3] && lateStale && h.Session.LateAnswersDropped == late0 + 2 && h.Session.ShownLoad == 0 &&
                  WmvRuntimeTerrainTile.Live == live0 && r.Material != null && requested == 4 && queued == unique - 4 &&
                  r.Material.Reason.EndsWith(unsettled, StringComparison.Ordinal),
                  "terrain client switch mid-texture: \"cleared\" with 2 textures out (retired, named) and 2 being prepared (not requests: their " +
                  "results dropped); the 2 \"stale\" answers dropped as late; the material reported with " + requested + " textures still " +
                  "\"requested\" and " + queued + " \"queued\", its reason \"" + (r != null && r.Material != null ? r.Material.Reason : "none") + "\"", log);
        }

        // ---- the timeout with textures out: one report, every pending texture retired
        {
            var h = new TerrainHarness(2000.0);
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(609, tx, ty));
            h.Session.Pump(100.0);
            host.Serve(h, h.Last(rw));
            host.Serve(h, h.Last(rr));
            host.Serve(h, h.Last(rt0));
            h.Session.Pump(200.0);
            List<string> ids = h.Session.PendingIds();
            h.Session.Pump(2100.5);
            WmvIpcClient.TerrainTileReport r = h.For(609);
            Check(ids.Count == 4 && r != null && r.Status == "failed" && r.Reason.StartsWith("timed out after 2 s waiting for the texture answer (request " + ids[0]) &&
                  r.RequestsRetired == 4 && h.CountFor(609) == 1 && h.Session.PendingRequests == 0,
                  "terrain timeout with textures out: one \"failed\" (" + (r != null ? r.Reason : "none") + "), all 4 retired", log);
        }

        // ---- a load that ends before its adoption reports its material settled with the failures answered so far: height
        // texture 992006 answered "not installed locally" while the other height textures are still out, then superseded --
        // its chunks (10, 17, 18) heightTextureMissing beside its entry, chunk 15 (its height texture unanswered) not
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(613, tx, ty));
            bool answered = false;
            for (int round = 0; round < 40 && !answered; round++)
            {
                foreach (string id in h.Session.PendingIds())
                {
                    string[] req = h.Sent.Find(s => s[0] == id);
                    uint fd = uint.Parse(req[2]);
                    if (req[1] == rtex && Array.IndexOf(built.HeightTextureIds, fd) >= 0)
                    {
                        if (fd == 992006u)
                        {
                            h.Answer(id, null, "not installed locally", "test: a height texture answered as a failure");
                            answered = true;
                        }
                    }
                    else
                        host.Serve(h, id);
                }
                h.Pump(1);
            }
            h.Pump(2);
            List<string> stillOut = h.Session.PendingIds();
            h.Session.Load(TexturedCommand(614, tx, ty));
            WmvIpcClient.TerrainTileReport r = h.For(613);
            TerrainTileMaterial want = SettledMaterial(tx, ty, MatHeightsOn, null, new[] { 992006u });
            int inRequest = r != null && r.Material != null
                ? r.Material.Textures.FindAll(t => t.State == WmvTerrainTexture.StateRequested || t.State == WmvTerrainTexture.StateQueued).Count : -1;
            WmvIpcClient.TerrainTextureReport failedEntry = TextureEntry(r, 992006, "height");
            string note = "the load ended before its textures settled: 3 texture(s) unanswered; chunk states count the answered failures only";
            Check(answered && stillOut.Count == 3 && r != null && r.Status == "superseded" && r.Material != null && inRequest == 3 &&
                  failedEntry != null && failedEntry.State == "not installed locally" &&
                  ChunkState(r, "heightTextureMissing") == want.Count(TerrainMaterialState.HeightTextureMissing) &&
                  want.Count(TerrainMaterialState.HeightTextureMissing) == 3 && ChunkState(r, "textured") == want.Count(TerrainMaterialState.Textured) &&
                  r.Material.State == TerrainTileMaterial.TileStateName(want.State) && r.Material.Reason.EndsWith(note, StringComparison.Ordinal),
                  string.Format("terrain load ended before adoption: \"superseded\" with height texture 992006 answered \"{0}\" and {1} still out -- " +
                                "chunks heightTextureMissing {2} (10, 17, 18; want {3}), textured {4} (want {5}); {6} entries in a request state; " +
                                "material {7}, reason \"{8}\"", failedEntry != null ? failedEntry.State : "none", stillOut.Count,
                                ChunkState(r, "heightTextureMissing"), want.Count(TerrainMaterialState.HeightTextureMissing), ChunkState(r, "textured"),
                                want.Count(TerrainMaterialState.Textured), inRequest, r != null && r.Material != null ? r.Material.State : "none",
                                r != null && r.Material != null ? r.Material.Reason : "none"), log);
            h.Session.Clear("test");
        }

        // ---- a height texture without an alpha channel (BLP alphaDepth 0) fails in its HEIGHT role only: T1's file (991001)
        // is, in this crafted tex0, also T8's height texture (MHID entry 8; never observed in real data) and is served as a
        // DXT1 -- one request, its diffuse role uploaded and drawn, its height role "decode failed", the chunks reading it as
        // a height texture (14, 17, 18) heightTextureMissing
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var mhid = (uint[])MatMhid.Clone();
            mhid[8] = MatMdid[0];
            byte[] tex0 = MaterialTestTex0(mhid);
            Dictionary<uint, byte[]> files = MaterialTestTextures();
            files[MatMdid[0]] = DxtBlp(1, 101, 16, 16);
            var host = new MaterialHost(tx, ty, tex0, files);
            h.Session.Load(TexturedCommand(615, tx, ty));
            host.ServeAll(h);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(615);
            TerrainTileMaterial want = TerrainMaterialBuilder.Build(AdtParser.ParseRoot(MaterialTestRoot(tx, ty), tx, ty), AdtParser.ParseTex(tex0), MatHeightsOn);
            bool bothRoles = Array.IndexOf(want.TextureIds, MatMdid[0]) >= 0 && Array.IndexOf(want.HeightTextureIds, MatMdid[0]) >= 0;
            int distinct = want.TextureIds.Length + want.HeightTextureIds.Length - 1;
            want.ApplyHeightTextureFailures(new[] { MatMdid[0], MatMissingHeight });
            int asked = 0;
            foreach (string[] s in h.Sent) if (s[1] == rtex && s[2] == MatMdid[0].ToString()) asked++;
            WmvIpcClient.TerrainTextureReport asDiffuse = TextureEntry(r, (int)MatMdid[0], "diffuse"), asHeight = TextureEntry(r, (int)MatMdid[0], "height");
            Check(r != null && r.Status == "built" && bothRoles && asked == 1 && TexturesSent(h) == distinct && BlpDecoder.ReadHeader(files[MatMdid[0]]).AlphaDepth == 0 &&
                  asDiffuse != null && asDiffuse.State == "available" && asDiffuse.UploadMs >= 0 &&
                  asHeight != null && asHeight.State == WmvTerrainTexture.StateDecodeFailed && asHeight.Detail == WmvTerrainTexture.NoAlphaChannel &&
                  asHeight.UploadMs < 0 && ChunkState(r, "heightTextureMissing") == want.Count(TerrainMaterialState.HeightTextureMissing) &&
                  want.Count(TerrainMaterialState.HeightTextureMissing) == 4 && ChunkState(r, "textured") == want.Count(TerrainMaterialState.Textured),
                  string.Format("terrain height texture without alpha: {0} asked once ({1} textures in all), diffuse entry \"{2}\" (uploaded {3:0.###} ms), " +
                                "height entry \"{4}\" (\"{5}\"); chunks heightTextureMissing {6} (14, 15, 17, 18; want {7}), textured {8} (want {9})",
                                MatMdid[0], TexturesSent(h), asDiffuse != null ? asDiffuse.State : "none", asDiffuse != null ? asDiffuse.UploadMs : -1,
                                asHeight != null ? asHeight.State : "none", asHeight != null ? asHeight.Detail : "", ChunkState(r, "heightTextureMissing"),
                                want.Count(TerrainMaterialState.HeightTextureMissing), ChunkState(r, "textured"), want.Count(TerrainMaterialState.Textured)), log);
            h.Session.Clear("test");
        }

        // ---- the ring past 128 with K: 40 loads each superseded with 4 textures out -> 160 retired, the newest 128 kept
        {
            var h = new TerrainHarness();
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            var all = new List<string>();
            for (int i = 0; i < 40; i++)
            {
                h.Session.Load(TexturedCommand(700 + i, tx, ty));
                host.Serve(h, h.Last(rw));
                host.Serve(h, h.Last(rr));
                host.Serve(h, h.Last(rt0));
                h.Pump(1);
                all.AddRange(h.Session.PendingIds());
            }
            h.Session.Clear("test");
            WmvTerrainSession.RetiredRequest e;
            bool newest = h.Session.TryGetRetired(all[all.Count - 1], out e) && h.Session.TryGetRetired(all[all.Count - 128], out e);
            bool oldest = !h.Session.TryGetRetired(all[all.Count - 129], out e) && !h.Session.TryGetRetired(all[0], out e);
            Check(all.Count == 160 && h.Session.RetiredTotal == 160 && h.Session.RetiredRequests == WmvTerrainSession.RetiredCapacity && newest && oldest &&
                  h.Session.OutstandingPeak <= WmvTerrainSession.MaxOutstanding,
                  "terrain ring with K: 40 loads ended with 4 textures out each retire 160 requests (" + h.Session.RetiredTotal + "), the ring keeps " +
                  "the newest " + h.Session.RetiredRequests + "; never more than " + h.Session.OutstandingPeak + " out", log);
        }

        // ---- many textured loads, every ending: conservation after each, at most K out, no collection growing
        {
            var h = new TerrainHarness(3000.0);
            harnesses.Add(h);
            var host = new MaterialHost(tx, ty);
            int applied = 0, unbalanced = 0;
            string at20 = null, atEnd = null, firstUnbalanced = null;
            int largest = 0;
            double now = 1e6;
            Func<string, bool> serve = id =>
            {
                bool a = host.Serve(h, id);
                if (a) applied++;
                return a;
            };
            for (int i = 0; i < 60; i++)
            {
                int load = 800 + i;
                h.Session.Load(TexturedCommand(load, tx, ty));
                serve(h.Last(rw));
                serve(h.Last(rr));
                serve(h.Last(rt0));
                h.Session.Pump(now += 16);
                switch (i % 5)
                {
                    case 0:                                                    // built
                        for (int k = 0; k < 40 && h.Session.Loading; k++)
                        {
                            foreach (string id in h.Session.PendingIds()) serve(id);
                            h.Session.Pump(now += 16);
                        }
                        break;
                    case 1: h.Session.Clear("test"); break;                      // cleared with textures out
                    case 2: serve(h.Session.PendingIds()[0]); break;             // superseded by the next with one being prepared
                    case 3: h.Session.Pump(now += 3001); break;                  // timed out
                    default: h.Session.LeaveForModelLoad("test model load"); break;
                }
                if (h.Sent.Count != applied + h.Session.RetiredTotal + h.Session.PendingRequests)
                {
                    unbalanced++;
                    if (firstUnbalanced == null)
                        firstUnbalanced = string.Format("after load {0}: {1} sent, {2} applied + {3} retired + {4} pending", load, h.Sent.Count, applied,
                                                        h.Session.RetiredTotal, h.Session.PendingRequests);
                }
                if (i == 22 || i == 57)
                {
                    // Both samples with a load superseded-to-be in flight, at the same step (case 2).
                    int l;
                    string sizes = SessionCollections(h.Session, out l);
                    if (i == 22) at20 = sizes; else { atEnd = sizes; largest = l; }
                }
            }
            h.Session.Clear("end");
            Check(unbalanced == 0 && h.Session.OutstandingPeak <= WmvTerrainSession.MaxOutstanding && at20 != null && at20 == atEnd &&
                  at20.Contains("current{") && h.Session.SecondReportsRefused == 0 && h.Reports.Count == 60,
                  "terrain textured loads: through 60 loads (built, cleared, superseded mid-preparation, timed out, a model load) every request " +
                  "sent is an answer applied, a retirement or pending (" + unbalanced + " not" + (firstUnbalanced != null ? "; " + firstUnbalanced : "") +
                  "), never more than " + h.Session.OutstandingPeak + " out, 60 reports; no collection grew -- after 23 loads " + at20 + ", after 58 " +
                  atEnd, log);
        }

        foreach (TerrainHarness x in harnesses)
            x.Session.Dispose();
        Check(WmvRuntimeTerrainTile.Live == live0 && TerrainTallies() == "0/0/0" && WmvRuntimeTerrainTile.LiveTextures == 0 &&
              WmvRuntimeTerrainTile.LiveAlphaArrays == 0,
              "terrain textured loads: every tile, texture and alpha array of the session checks is released", log);
    }

    // ================================================================ protocol 13 lines

    [Serializable] class MaterialLineProbe
    {
        public int load;
        public int requestsRetired;
        public string[] retiredRequestIds;
        public MaterialProbe material;
    }

    [Serializable] class MaterialProbe
    {
        public string state;
        public string reason;
        public int tex0FileDataID;
        public string alphaFormat;
        public TextureProbe[] textures;
        public int uniqueTextures;
        public int materials;
    }

    [Serializable] class TextureProbe
    {
        public int fileDataID;
        public string role;
        public string state;
        public string detail;
        public int width;
        public string format;
        public int mips;
    }

    static void TerrainMaterialIpcTests(Action<string> log)
    {
        var go = new GameObject("TerrainMaterialIpcTest");
        try
        {
            var ipc = go.AddComponent<WmvIpcClient>();
            var sent = new List<string>();
            WmvIpcClient.TerrainTileCommand got = null;
            ipc.SentForTest = s => sent.Add(s);
            ipc.OnLoadTerrainTile = c => got = c;
            ipc.FeedLineForTest("{\"type\":\"loadTerrainTile\",\"load\":5,\"assetEpoch\":3,\"mapId\":0,\"mapDirectory\":\"Azeroth\",\"product\":\"wow\"," +
                                "\"build\":\"12.1.0.69933\",\"wdtFileDataID\":775971,\"wdtResolvedBy\":\"fileDataID\",\"tileX\":32,\"tileY\":50," +
                                "\"maxAssetBytes\":4194304,\"maxTextureBytes\":8388608,\"maxTextureBytesPerLoad\":134217728,\"maxTexturesPerLoad\":128}", null);
            WmvIpcClient.TerrainTileCommand old = null;
            ipc.OnLoadTerrainTile = c => old = c;
            ipc.FeedLineForTest("{\"type\":\"loadTerrainTile\",\"load\":6,\"assetEpoch\":3,\"mapId\":0,\"wdtFileDataID\":775971,\"tileX\":32,\"tileY\":50," +
                                "\"maxAssetBytes\":4194304}", null);
            Check(got != null && got.MaxTextureBytes == 8388608 && got.MaxTextureBytesPerLoad == 134217728 && got.MaxTexturesPerLoad == 128 &&
                  got.TexturesOffered && got.Build == "12.1.0.69933" && old != null && !old.TexturesOffered && old.MaxTexturesPerLoad == 0,
                  "terrain ipc 13: loadTerrainTile's texture limits and build are read (" + got + "); a protocol-12 line offers none (" + old + ")", log);

            string tex0 = ipc.RequestMapAsset(WmvTerrainSession.RoleTex0Adt, 778040, 5, 3);
            string tex = ipc.RequestMapAsset(WmvTerrainSession.RoleTexture, 186934, 5, 3);
            Check(sent.Count == 2 && sent[0] == "{\"type\":\"getMapAsset\",\"requestId\":\"" + tex0 + "\",\"load\":5,\"assetEpoch\":3,\"role\":\"tex0Adt\",\"fileDataID\":778040}" &&
                  sent[1].Contains("\"role\":\"texture\",\"fileDataID\":186934"),
                  "terrain ipc 13: getMapAsset asks for the tex0 ADT and a texture under their roles (" + string.Join(" ", sent.ToArray()) + ")", log);

            var report = new WmvIpcClient.TerrainTileReport
            {
                Load = 5, Status = "built", RequestsRetired = 2, RetiredRequestIds = new List<string> { "map7", "map8" },
                Material = new WmvIpcClient.TerrainMaterialReport
                {
                    State = "partial", Reason = "245 of 256 chunks textured; layerEffectStatic 11", Tex0FileDataID = 778420, Tex0Bytes = 1234,
                    Tex0Md5 = "ab", AlphaFormat = "8-bit", UniqueTextures = 2, LayerReferences = 9, DuplicateReferences = 7, Materials = 256,
                    TextureSourceBytes = 99, TextureGpuBytes = 88, AlphaGpuBytes = 16384, MaterialVertexBytes = 1160, Tex0RoundTripMs = 1.5,
                    MaterialBuildMs = 2.25,
                    ChunkStates = new List<KeyValuePair<string, int>> { new KeyValuePair<string, int>("textured", 245), new KeyValuePair<string, int>("layerEffectStatic", 11) },
                    Textures = new List<WmvIpcClient.TerrainTextureReport>
                    {
                        new WmvIpcClient.TerrainTextureReport { FileDataID = 186934, State = "available", Bytes = 65680, Md5 = "cd", Width = 256, Height = 256,
                                                                Format = "RGBA_DXT3_UNorm", Mips = 9, UploadMs = 0.5 },
                        new WmvIpcClient.TerrainTextureReport { FileDataID = 1, State = "not installed locally", Detail = "a \"quoted\" detail" },
                        new WmvIpcClient.TerrainTextureReport { FileDataID = 971415, Role = "height", State = "available", Bytes = 87536, Md5 = "ef",
                                                                Width = 256, Height = 256, Format = "RGBA_DXT5_UNorm", Mips = 9 },
                    },
                },
            };
            string line = WmvIpcClient.TerrainTileLoadedLine(report);
            MaterialLineProbe p = JsonUtility.FromJson<MaterialLineProbe>(line);
            Check(p != null && p.load == 5 && p.requestsRetired == 2 && p.retiredRequestIds != null && p.retiredRequestIds.Length == 2 &&
                  p.retiredRequestIds[1] == "map8" && p.material != null && p.material.state == "partial" && p.material.tex0FileDataID == 778420 &&
                  p.material.alphaFormat == "8-bit" && p.material.textures != null && p.material.textures.Length == 3 &&
                  p.material.textures[0].format == "RGBA_DXT3_UNorm" && p.material.textures[0].mips == 9 && p.material.textures[1].detail == "a \"quoted\" detail" &&
                  p.material.textures[0].role == "diffuse" && p.material.textures[1].role == "diffuse" && p.material.textures[2].role == "height" &&
                  p.material.uniqueTextures == 2 && p.material.materials == 256 &&
                  line.Contains("\"chunkStates\":{\"textured\":245,\"layerEffectStatic\":11}") &&
                  line.Contains("{\"fileDataID\":971415,\"role\":\"height\",\"state\":\"available\"") &&
                  line.Contains("\"timings\":{\"tex0RoundTripMs\":1.5,\"materialBuildMs\":2.25}}") && !line.Contains("\"width\":-1"),
                  "terrain ipc 13: terrainTileLoaded carries requestsRetired, the retired ids and the material with every texture's role, valid " +
                  "JSON (" + (line.Length > 200 ? line.Substring(line.IndexOf("\"requestsRetired\"", StringComparison.Ordinal)) : line) + ")", log);

            sent.Clear();
            ipc.ReportRuntimeState(11, new WmvIpcClient.RuntimeReport { MountKey = "", TerrainLastRetiredRequest = "", TerrainTextures = 10,
                                                                         TerrainTextureBytes = 655360, TerrainAlphaArrays = 1 });
            Check(sent.Count == 1 && sent[0].EndsWith(",\"terrainTextures\":10,\"terrainTextureBytes\":655360,\"terrainAlphaArrays\":1}"),
                  "terrain ipc 13: runtimeState carries terrainTextures, terrainTextureBytes and terrainAlphaArrays", log);
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(go);
        }
    }
}
