// AdtSynthetic.cs
//
// Synthetic WDT / split-ADT byte images for tests that need a world file with a KNOWN shape rather
// than a client file: the runner's parser and terrain-mesh tests, the netstandard compile check and
// the player's self-test all build the same bytes from here. No Unity type in this file, and no game
// content: only format structure (chunk tags, header offsets) as the audit established it
// (docs/world-viewer/00-audit.md, AdtParser.cs, WdtParser.cs).
//
// Root MCNK layout written by RootMcnk: the 128-byte header (flags +0, index x/y +4/+8, 64-bit hole
// mask +20, area +52, 16-bit hole field +60, stored position +104), then MCVT (145 floats, unless a
// spec says otherwise), MCCV (when asked), MCNR (448 bytes), MCSE (empty). By default the stored
// position is the chunk's corner from TerrainCoordinates rounded to float32, the base height 50 and
// the heights k * 0.25.
//
// tex0 written by Tex0 (terrain materials): MVER, MAMP (4 zero bytes), MDID, MHID, 256 MCNKs of MCLY,
// MCSH and MCAL (alpha maps back to back: RLE row by row, raw 4096 bytes, or raw 2048 bytes low nibble
// first), then MTXP (16 bytes per MDID entry) when asked, then any extra top-level chunks (repeats).

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>What RootMcnk writes for one root MCNK. Every field has the well-formed default.</summary>
    public sealed class RootChunkSpec
    {
        /// <summary>MCNK flags; 0x10000 = the 64-bit hole mask at +20 is in use (every audited chunk).</summary>
        public uint Flags = 0x10000;
        /// <summary>64-bit hole mask at +20, bit row*8 + col, LSB first.</summary>
        public ulong Holes;
        /// <summary>16-bit hole field at +60 (its layout is not established; non-zero with flag 0x10000 clear
        /// makes the hole query unanswerable).</summary>
        public ushort LowResolutionHoles;
        /// <summary>Write an MCCV (and set flag 0x40).</summary>
        public bool VertexColors;
        /// <summary>The MCCV payload to write (145 x B, G, R, A; implies an MCCV); null = 580 zero bytes
        /// when VertexColors is set.</summary>
        public byte[] VertexColorBytes;
        /// <summary>Set flag 0x40 with the MCCV (every audited chunk does); false writes the MCCV without it.</summary>
        public bool VertexColorFlag = true;
        /// <summary>Stored position z: the base the MCVT heights are relative to.</summary>
        public float BaseHeight = 50f;
        /// <summary>MCVT entry k -> height relative to BaseHeight; null = k * 0.25.</summary>
        public Func<int, float> Height;
        /// <summary>Byte size of the MCVT payload (580 = 145 floats is well formed); -1 = no MCVT at all.</summary>
        public int McvtBytes = 580;
        /// <summary>Yards added to the chunk corner before the stored x/y are rounded to float32.</summary>
        public double StoredXOffset, StoredYOffset;
        /// <summary>Store the corner of this tile instead of the tile the root is built for (a root of
        /// another tile); -1 = the root's own tile.</summary>
        public int StoredTileX = -1, StoredTileY = -1;
        /// <summary>Header index x/y to write; -1 = the chunk's own position (n % 16, n / 16).</summary>
        public int HeaderIndexX = -1, HeaderIndexY = -1;
        /// <summary>Overrides the stored x (raw float32, e.g. NaN); null = from the corner.</summary>
        public float? StoredX;
    }

    public static class AdtSynthetic
    {
        // ------------------------------------------------------------------ byte helpers

        public static void PutU16(byte[] b, int o, ushort v) { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); }

        public static void PutU32(byte[] b, int o, uint v)
        {
            b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 16); b[o + 3] = (byte)(v >> 24);
        }

        public static void PutU64(byte[] b, int o, ulong v) { PutU32(b, o, (uint)v); PutU32(b, o + 4, (uint)(v >> 32)); }

        public static void PutF32(byte[] b, int o, float f) { PutU32(b, o, BitConverter.ToUInt32(BitConverter.GetBytes(f), 0)); }

        /// <summary>One chunk: the tag stored reversed, the u32 size, the payload.</summary>
        public static byte[] Chunk(string tag, byte[] payload)
        {
            if (tag == null || tag.Length != 4)
                throw new ArgumentException("chunk tags have four characters");
            payload = payload ?? new byte[0];
            var b = new byte[8 + payload.Length];
            b[0] = (byte)tag[3]; b[1] = (byte)tag[2]; b[2] = (byte)tag[1]; b[3] = (byte)tag[0];
            PutU32(b, 4, (uint)payload.Length);
            Buffer.BlockCopy(payload, 0, b, 8, payload.Length);
            return b;
        }

        public static byte[] Cat(params byte[][] parts)
        {
            int n = 0;
            foreach (byte[] p in parts) n += p.Length;
            var r = new byte[n];
            int o = 0;
            foreach (byte[] p in parts) { Buffer.BlockCopy(p, 0, r, o, p.Length); o += p.Length; }
            return r;
        }

        public static byte[] U32Payload(params uint[] v)
        {
            var b = new byte[4 * v.Length];
            for (int i = 0; i < v.Length; i++) PutU32(b, 4 * i, v[i]);
            return b;
        }

        public static byte[] Mver() { return Chunk("MVER", U32Payload(18)); }

        // ------------------------------------------------------------------ WDT

        public static byte[] Mphd(uint flags, params uint[] words)
        {
            var w = new uint[8];
            w[0] = flags;
            for (int i = 0; i < words.Length && i < 7; i++) w[i + 1] = words[i];
            return Chunk("MPHD", U32Payload(w));
        }

        /// <summary>MAIN with flag 0x1 on the given tile indices (y*64 + x).</summary>
        public static byte[] MainTable(params int[] tileIndices)
        {
            var b = new byte[4096 * 8];
            foreach (int i in tileIndices) PutU32(b, 8 * i, 1);
            return Chunk("MAIN", b);
        }

        /// <summary>MAID with the given words (root, obj0, obj1, tex0, lod, map texture, _n, minimap) on one tile.</summary>
        public static byte[] Maid(int tileIndex, params uint[] words)
        {
            var b = new byte[4096 * 32];
            for (int k = 0; k < words.Length; k++) PutU32(b, tileIndex * 32 + 4 * k, words[k]);
            return Chunk("MAID", b);
        }

        public static byte[] ModfRecord(uint name, uint uid, ushort flags, ushort scale)
        {
            var r = new byte[64];
            PutU32(r, 0, name);
            PutU32(r, 4, uid);
            for (int k = 0; k < 6; k++) PutF32(r, 8 + 4 * k, 100f + k);       // position, rotation
            for (int k = 0; k < 6; k++) PutF32(r, 32 + 4 * k, 10f * (k + 1)); // extents
            PutU16(r, 56, flags);
            PutU16(r, 58, 3);
            PutU16(r, 60, 4);
            PutU16(r, 62, scale);
            return r;
        }

        public static byte[] MddfRecord(uint name, uint uid, float px, float py, float pz, float a0, float a1, float a2, ushort scale, ushort flags)
        {
            var r = new byte[36];
            PutU32(r, 0, name);
            PutU32(r, 4, uid);
            PutF32(r, 8, px); PutF32(r, 12, py); PutF32(r, 16, pz);
            PutF32(r, 20, a0); PutF32(r, 24, a1); PutF32(r, 28, a2);
            PutU16(r, 32, scale);
            PutU16(r, 34, flags);
            return r;
        }

        // ------------------------------------------------------------------ root ADT

        /// <summary>A root MCNK: 128-byte header + MCVT (+ MCCV) + MCNR + MCSE.</summary>
        public static byte[] RootMcnk(int index, uint flags, ulong holes, bool mccv, int tileX, int tileY, Func<int, float> height = null)
        {
            return RootMcnk(index, tileX, tileY, new RootChunkSpec { Flags = flags, Holes = holes, VertexColors = mccv, Height = height });
        }

        /// <summary>A root MCNK as `spec` describes it (null = the well-formed default).</summary>
        public static byte[] RootMcnk(int index, int tileX, int tileY, RootChunkSpec spec)
        {
            spec = spec ?? new RootChunkSpec();
            bool mccv = spec.VertexColors || spec.VertexColorBytes != null;
            var h = new byte[128];
            PutU32(h, 0, spec.Flags | (mccv && spec.VertexColorFlag ? 0x40u : 0u));
            PutU32(h, 4, (uint)(spec.HeaderIndexX >= 0 ? spec.HeaderIndexX : index % 16));
            PutU32(h, 8, (uint)(spec.HeaderIndexY >= 0 ? spec.HeaderIndexY : index / 16));
            PutU64(h, 20, spec.Holes);
            PutU32(h, 52, 12);
            PutU16(h, 60, spec.LowResolutionHoles);
            double x, y;
            TerrainCoordinates.ChunkOrigin(spec.StoredTileX >= 0 ? spec.StoredTileX : tileX, spec.StoredTileY >= 0 ? spec.StoredTileY : tileY,
                                           index % 16, index / 16, out x, out y);
            PutF32(h, 104, spec.StoredX ?? (float)(x + spec.StoredXOffset));
            PutF32(h, 108, (float)(y + spec.StoredYOffset));
            PutF32(h, 112, spec.BaseHeight);
            byte[] mcvtChunk = new byte[0];
            if (spec.McvtBytes >= 0)
            {
                var mcvt = new byte[spec.McvtBytes];
                for (int k = 0; k < 145 && 4 * k + 4 <= mcvt.Length; k++)
                    PutF32(mcvt, 4 * k, spec.Height != null ? spec.Height(k) : k * 0.25f);
                mcvtChunk = Chunk("MCVT", mcvt);
            }
            var mcnr = new byte[448];
            for (int k = 0; k < 435; k++) mcnr[k] = (byte)(k % 127);
            byte[] sub = Cat(mcvtChunk, mccv ? Chunk("MCCV", spec.VertexColorBytes ?? new byte[580]) : new byte[0], Chunk("MCNR", mcnr), Chunk("MCSE", new byte[0]));
            return Chunk("MCNK", Cat(h, sub));
        }

        /// <summary>A root: MVER, `extra` (e.g. MH2O), then 256 MCNKs -- the default chunk unless
        /// `mcnkOverride` returns bytes for that index.</summary>
        public static byte[] Root(int tileX, int tileY, Func<int, byte[]> mcnkOverride = null, byte[] extra = null)
        {
            var parts = new List<byte[]> { Mver() };
            if (extra != null) parts.Add(extra);
            for (int i = 0; i < 256; i++)
            {
                byte[] o = mcnkOverride != null ? mcnkOverride(i) : null;
                parts.Add(o ?? RootMcnk(i, 0x10000, 0, false, tileX, tileY));
            }
            return Cat(parts.ToArray());
        }

        /// <summary>A root whose chunk n is RootMcnk(n, tileX, tileY, spec(n)).</summary>
        public static byte[] RootWith(int tileX, int tileY, Func<int, RootChunkSpec> spec)
        {
            return Root(tileX, tileY, i => RootMcnk(i, tileX, tileY, spec != null ? spec(i) : null));
        }

        /// <summary>Every vertex of the tile at height z (base z, relative heights 0).</summary>
        public static byte[] FlatRoot(int tileX, int tileY, float z = 50f)
        {
            return RootWith(tileX, tileY, n => new RootChunkSpec { BaseHeight = z, Height = k => 0f });
        }

        /// <summary>MCVT heights of chunk `index` following a world-space height field z(x, y) (game
        /// frame, yards), relative to `baseHeight`: vertex k gets (float)(z(x_k, y_k) - baseHeight),
        /// with x_k, y_k from TerrainMeshBuilder.VertexWorldXY.</summary>
        public static Func<int, float> WorldHeights(int tileX, int tileY, int index, Func<double, double, double> z, float baseHeight)
        {
            return k =>
            {
                double x, y;
                TerrainMeshBuilder.VertexWorldXY(tileX, tileY, index % 16, index / 16, k, out x, out y);
                return (float)(z(x, y) - baseHeight);
            };
        }

        /// <summary>A plane through the tile: z = baseHeight + slopeX (x - X0) + slopeY (y - Y0), X0/Y0
        /// the tile's north-west corner (world +x is north, +y west). Asymmetric ramps for orientation tests.</summary>
        public static byte[] RampRoot(int tileX, int tileY, double slopeX, double slopeY, float baseHeight = 100f)
        {
            WorldOrigin o = WorldToUnity.TileOrigin(tileX, tileY);
            Func<double, double, double> z = (x, y) => baseHeight + slopeX * (x - o.X) + slopeY * (y - o.Y);
            return RootWith(tileX, tileY, n => new RootChunkSpec { BaseHeight = baseHeight, Height = WorldHeights(tileX, tileY, n, z, baseHeight) });
        }

        /// <summary>A root MCNK smaller than its 128-byte header (malformed; the parser reads nothing of it).</summary>
        public static byte[] ShortMcnk(int size = 100) { return Chunk("MCNK", new byte[size]); }

        /// <summary>A root MCNK like RootMcnk, with one extra sub-chunk appended.</summary>
        public static byte[] RootMcnkWith(int index, int tileX, int tileY, byte[] extra)
        {
            byte[] m = RootMcnk(index, 0x10000, 0, false, tileX, tileY);
            var payload = new byte[m.Length - 8];
            Buffer.BlockCopy(m, 8, payload, 0, payload.Length);
            return Chunk("MCNK", Cat(payload, extra));
        }

        /// <summary>A root MCNK with the standard header and exactly the given sub-chunks.</summary>
        public static byte[] RootMcnkOnly(int index, int tileX, int tileY, params byte[][] subs)
        {
            byte[] m = RootMcnk(index, 0x10000, 0, false, tileX, tileY);
            var header = new byte[128];
            Buffer.BlockCopy(m, 8, header, 0, 128);
            return Chunk("MCNK", Cat(header, Cat(subs)));
        }

        // ------------------------------------------------------------------ tex0

        public static byte[] Mcly(params uint[] layers) // texture, flags, offset, word12 per layer
        {
            return Chunk("MCLY", U32Payload(layers));
        }

        public static byte[] TexMcnk(byte[] mcly, byte[] mcal) { return Chunk("MCNK", Cat(mcly ?? new byte[0], mcal ?? new byte[0])); }

        public static byte[] Tex(Func<int, byte[]> mcnk, int textures = 4)
        {
            var mdid = new uint[textures];
            for (int i = 0; i < textures; i++) mdid[i] = 1000u + (uint)i;
            var parts = new List<byte[]> { Mver(), Chunk("MAMP", new byte[4]), Chunk("MDID", U32Payload(mdid)), Chunk("MHID", U32Payload(new uint[textures])) };
            for (int i = 0; i < 256; i++)
                parts.Add(mcnk(i) ?? TexMcnk(Mcly(0, 0, 0, 0), null));
            return Cat(parts.ToArray());
        }

        /// <summary>RLE: a whole 4096-value map filled with `value` (32 runs of 127 + one of 32).</summary>
        public static byte[] RleFill(byte value) { return RleFill(value, 4096); }

        /// <summary>RLE fill runs covering exactly `count` values.</summary>
        public static byte[] RleFill(byte value, int count)
        {
            var b = new List<byte>();
            int left = count;
            while (left > 0)
            {
                int n = Math.Min(127, left);
                b.Add((byte)(0x80 | n));
                b.Add(value);
                left -= n;
            }
            return b.ToArray();
        }

        // ------------------------------------------------------------------ tex0 with materials

        /// <summary>RLE of 4096 alpha values, row by row (no run crosses a 64-texel row, as in every audited
        /// map): a fill run (0x80 | n, value) for 2..64 equal values, copy runs (n, n bytes) for the rest.</summary>
        public static byte[] RleEncode(byte[] values)
        {
            if (values == null || values.Length != 4096)
                throw new ArgumentException("an alpha map has 4096 values");
            var b = new List<byte>();
            for (int row = 0; row < 64; row++)
            {
                int p = row * 64, end = p + 64;
                var copy = new List<byte>();
                while (p < end)
                {
                    int run = 1;
                    while (p + run < end && values[p + run] == values[p])
                        run++;
                    if (run >= 2)
                    {
                        FlushCopy(b, copy);
                        b.Add((byte)(0x80 | run));
                        b.Add(values[p]);
                    }
                    else
                        copy.Add(values[p]);
                    p += run;
                }
                FlushCopy(b, copy);
            }
            return b.ToArray();
        }

        static void FlushCopy(List<byte> b, List<byte> copy)
        {
            if (copy.Count == 0)
                return;
            b.Add((byte)copy.Count);
            b.AddRange(copy);
            copy.Clear();
        }

        /// <summary>A 4-bit map of 4096 values 0..15 as stored: 2048 bytes, low nibble first.</summary>
        public static byte[] Raw4(byte[] nibbles)
        {
            if (nibbles == null || nibbles.Length != 4096)
                throw new ArgumentException("an alpha map has 4096 values");
            var b = new byte[2048];
            for (int i = 0; i < 2048; i++)
                b[i] = (byte)((nibbles[2 * i] & 0x0F) | ((nibbles[2 * i + 1] & 0x0F) << 4));
            return b;
        }

        /// <summary>A 4096-value alpha map from a function of (row, col).</summary>
        public static byte[] AlphaMapOf(Func<int, int, int> value)
        {
            var v = new byte[4096];
            for (int r = 0; r < 64; r++)
                for (int c = 0; c < 64; c++)
                    v[r * 64 + c] = (byte)value(r, c);
            return v;
        }

        /// <summary>One MTXP entry (16 bytes): flags (bits 4-7 = texture-scale exponent), height scale,
        /// height offset, word 3. The audited defaults are (0, 0.0, 1.0, 0).</summary>
        public static byte[] MtxpEntry(uint flags, float heightScale = 0f, float heightOffset = 1f, uint word3 = 0)
        {
            var b = new byte[16];
            PutU32(b, 0, flags);
            PutF32(b, 4, heightScale);
            PutF32(b, 8, heightOffset);
            PutU32(b, 12, word3);
            return b;
        }

        /// <summary>One MCLY layer and its alpha map for TexMcnkFrom.</summary>
        public sealed class TexLayerSpec
        {
            public uint TextureIndex;
            /// <summary>Flag bits added to the ones written for the layer's role: 0x100 on every layer after
            /// the first, 0x200 on an RLE layer.</summary>
            public uint ExtraFlags;
            /// <summary>Write exactly these flags instead (null = role bits | ExtraFlags).</summary>
            public uint? Flags;
            public AlphaStorage Storage = AlphaStorage.Compressed;
            /// <summary>4096 values (0..15 for Uncompressed4); null = all 0. Not written for the first layer.</summary>
            public byte[] Alpha;
            public uint Word12;
            /// <summary>Write this MCAL offset instead of the running one.</summary>
            public uint? AlphaOffset;
        }

        /// <summary>A tex0 MCNK: MCLY from the layers (none: an empty MCLY, unless OmitMcly), MCSH, then the
        /// layers' alpha maps back to back in MCAL (none for a single layer, unless McalOverride).</summary>
        public sealed class TexChunkSpec
        {
            public TexLayerSpec[] Layers = new TexLayerSpec[0];
            public bool OmitMcly;
            public byte[] Shadow;
            public byte[] McalOverride;

            public static TexChunkSpec Of(params TexLayerSpec[] layers) { return new TexChunkSpec { Layers = layers }; }
        }

        public static TexLayerSpec Layer(uint textureIndex, AlphaStorage storage = AlphaStorage.Compressed, byte[] alpha = null, uint extraFlags = 0)
        {
            return new TexLayerSpec { TextureIndex = textureIndex, Storage = storage, Alpha = alpha, ExtraFlags = extraFlags };
        }

        public static byte[] TexMcnkFrom(TexChunkSpec spec)
        {
            spec = spec ?? new TexChunkSpec();
            TexLayerSpec[] layers = spec.Layers ?? new TexLayerSpec[0];
            var mcly = new byte[16 * layers.Length];
            var mcal = new List<byte>();
            for (int k = 0; k < layers.Length; k++)
            {
                TexLayerSpec l = layers[k];
                byte[] stored = new byte[0];
                uint role = 0;
                if (k >= 1)
                {
                    byte[] values = l.Alpha ?? new byte[4096];
                    role = 0x100;
                    if (l.Storage == AlphaStorage.Compressed) { role |= 0x200; stored = RleEncode(values); }
                    else if (l.Storage == AlphaStorage.Uncompressed8) stored = (byte[])values.Clone();
                    else stored = Raw4(values);
                }
                PutU32(mcly, 16 * k, l.TextureIndex);
                PutU32(mcly, 16 * k + 4, l.Flags ?? (role | l.ExtraFlags));
                PutU32(mcly, 16 * k + 8, l.AlphaOffset ?? (k >= 1 ? (uint)mcal.Count : 0u));
                PutU32(mcly, 16 * k + 12, l.Word12);
                mcal.AddRange(stored);
            }
            var parts = new List<byte[]>();
            if (!spec.OmitMcly) parts.Add(Chunk("MCLY", mcly));
            if (spec.Shadow != null) parts.Add(Chunk("MCSH", spec.Shadow));
            if (spec.McalOverride != null) parts.Add(Chunk("MCAL", spec.McalOverride));
            else if (layers.Length > 1) parts.Add(Chunk("MCAL", mcal.ToArray()));
            return Chunk("MCNK", Cat(parts.ToArray()));
        }

        /// <summary>What Tex0 writes: MVER, MAMP (4 zero bytes), MDID, MHID (zeros of MDID's length when null),
        /// the 256 MCNKs, then MTXP when given -- the order of the audited files with MTXP.</summary>
        public sealed class Tex0Spec
        {
            public uint[] Mdid = { 1000, 1001, 1002, 1003 };
            public uint[] Mhid;
            /// <summary>The MHID payload as given (e.g. not whole u32s); overrides Mhid.</summary>
            public byte[] MhidRaw;
            public bool OmitMhid;
            /// <summary>MTXP payload (e.g. MtxpEntry per MDID entry, concatenated); null = no MTXP.</summary>
            public byte[] Mtxp;
            /// <summary>MCNK n; a null spec (or a null function) = one base layer naming MDID entry 0.</summary>
            public Func<int, TexChunkSpec> Chunk;
            /// <summary>Whole chunks (Chunk(tag, payload)) written last, e.g. a second MDID, MHID or MTXP.</summary>
            public byte[][] Extra;
        }

        public static byte[] Tex0(Tex0Spec spec)
        {
            spec = spec ?? new Tex0Spec();
            uint[] mdid = spec.Mdid ?? new uint[0];
            var parts = new List<byte[]> { Mver(), Chunk("MAMP", new byte[4]), Chunk("MDID", U32Payload(mdid)) };
            if (!spec.OmitMhid)
                parts.Add(Chunk("MHID", spec.MhidRaw ?? U32Payload(spec.Mhid ?? new uint[mdid.Length])));
            for (int i = 0; i < 256; i++)
            {
                TexChunkSpec c = spec.Chunk != null ? spec.Chunk(i) : null;
                parts.Add(TexMcnkFrom(c ?? TexChunkSpec.Of(Layer(0))));
            }
            if (spec.Mtxp != null)
                parts.Add(Chunk("MTXP", spec.Mtxp));
            if (spec.Extra != null)
                parts.AddRange(spec.Extra);
            return Cat(parts.ToArray());
        }
    }
}
