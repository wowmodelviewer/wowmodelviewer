// BlpDecoder.cs
//
// Runtime BLP2 decoder: the texture bytes WMV serves over IPC become a plain RGBA32 pixel
// array the Unity side uploads straight into a Texture2D. Nothing is written to disk -- no
// PNG, no DDS, no intermediate file of any kind.
//
// BLP2 header (little-endian):
//   0x00  char   magic[4]        "BLP2"
//   0x04  uint32 version         1
//   0x08  uint8  colorEncoding   1 = palettized, 2 = DXT, 3 = uncompressed BGRA
//   0x09  uint8  alphaSize       0, 1, 4 or 8 bits
//   0x0A  uint8  preferredFormat DXT selector when colorEncoding == 2
//   0x0B  uint8  hasMips
//   0x0C  uint32 width
//   0x10  uint32 height
//   0x14  uint32 mipOffsets[16]
//   0x54  uint32 mipSizes[16]
//   0x94  uint32 palette[256]    BGRA, only meaningful for colorEncoding 1
//   0x494 pixel data
//
// SUPPORTED ENCODINGS
//   1 (palettized, alphaSize 0/1/4/8) and 2 (DXT1/DXT3/DXT5) -- between them these cover
//   creature skins, which is what this milestone renders. Encoding 3 (raw BGRA) is also
//   handled since it is a two-line case. Anything else raises WowParseException naming the
//   encoding, so an unsupported texture is reported rather than decoded into garbage.
//
// HEADER AND MIP TABLE (ReadHeader)
//   For a caller that uploads the stored blocks itself (terrain textures, stage 04): the format
//   Decode would use, the alpha depth, the size and every stored mip level -- its offset, its stored
//   size and the bytes one level of that format and size occupies. Nothing is decoded. The stored
//   levels are the ones with a non-zero size counted from level 0 up to the first zero (at most the
//   full chain down to 1x1); each is checked against the file as Decode checks the level it decodes.
//
// DXT DECODE MODES (Decode's exactBlocks)
//   Default (models, world models and their pinned captures; unchanged): 565 endpoints expanded as
//   v * 255 / 31 and v * 255 / 63, every interpolant truncated -- up to 1/255 below the format's decode.
//   Opt-in exact blocks (terrain textures that cannot be uploaded as blocks, stage 04): the block
//   format's own decode, rounded to the nearest 8-bit value where RGBA32 cannot hold it -- 565 by bit
//   replication ((r << 3) | (r >> 2), (g << 2) | (g >> 4)), colour interpolants (2a + b) / 3 and
//   (a + 2b) / 3 and DXT5 alpha ramps computed unrounded then rounded to nearest (no ties occur there),
//   the DXT1 three-colour midpoint (a + b) / 2 rounded to nearest with ties to even (an odd sum is the
//   only tie), DXT1 three-colour index 3 = (0, 0, 0, 0), DXT3 alpha nibble * 17. DXT3/DXT5 colour
//   blocks are four-colour in both modes.

using System;

namespace Wmv.Wow
{
    public class BlpImage
    {
        public int Width, Height;
        public byte[] Rgba;          // Width*Height*4, RGBA order, top row first
        public int MipLevel;
        public string Encoding = ""; // human-readable, for diagnostics
        public bool HasAlpha;
        /// <summary>Stage 08: the stored DXT levels flipped for the model convention (BlpBlocks.PrepareForModels), when the
        /// image goes up as its own blocks; Rgba is then null.</summary>
        public BlpBlockChain Blocks;
        /// <summary>Why a file prepared for a block upload was decoded instead (null: blocks, or never asked).</summary>
        public string BlocksNotUsed;
        /// <summary>The bytes the image holds: its RGBA32 pixels or its blocks.</summary>
        public long HeldBytes { get { return Rgba != null ? Rgba.LongLength : Blocks != null ? Blocks.Bytes : 0; } }
    }

    /// <summary>The pixel format of a BLP2 file, as Decode chooses it.</summary>
    public enum BlpPixelFormat
    {
        /// <summary>colorEncoding 1: 8-bit palette indices, then 0/1/4/8-bit alpha.</summary>
        Palettized,
        /// <summary>colorEncoding 2 with preferredFormat 0 or alphaSize 0/1: 8-byte blocks.</summary>
        Dxt1,
        /// <summary>colorEncoding 2, preferredFormat 1 (alphaSize above 1): 16-byte blocks, explicit 4-bit alpha.</summary>
        Dxt3,
        /// <summary>colorEncoding 2, any other preferredFormat (alphaSize above 1): 16-byte blocks, interpolated alpha.</summary>
        Dxt5,
        /// <summary>colorEncoding 3: 4 bytes per pixel, BGRA.</summary>
        RawBgra,
    }

    /// <summary>One stored mip level of a BLP2 file.</summary>
    public struct BlpMipLevel
    {
        public int Level;
        public int Width, Height;     // max(1, size >> level)
        public int Offset;            // byte offset of the level in the file
        public int StoredSize;        // the mip table's size for the level
        public int DataSize;          // bytes one level of this format and size occupies (DXT: blocks x block bytes)
    }

    /// <summary>A BLP2 header and its stored mip levels (BlpDecoder.ReadHeader). Rows are as stored:
    /// row 0 first, as Decode returns them.</summary>
    public sealed class BlpHeader
    {
        public int ColorEncoding;     // 0x08
        public int AlphaDepth;        // 0x09: 0, 1, 4 or 8 bits
        public int PreferredFormat;   // 0x0A
        public int HasMipsRaw;        // 0x0B, as stored (1 and 2 both occur with full chains)
        public int Width, Height;
        public BlpPixelFormat Format;
        public string Encoding = "";  // the name Decode reports: dxt1/dxt3/dxt5, palettized/a<n>, bgra8888
        public BlpMipLevel[] Mips;    // stored levels 0..MipCount-1

        public int MipCount { get { return Mips == null ? 0 : Mips.Length; } }
        public bool IsBlockCompressed { get { return Format == BlpPixelFormat.Dxt1 || Format == BlpPixelFormat.Dxt3 || Format == BlpPixelFormat.Dxt5; } }
        /// <summary>Bytes per 4x4 block: 8 (DXT1), 16 (DXT3/DXT5), 0 for the other formats.</summary>
        public int BlockBytes { get { return Format == BlpPixelFormat.Dxt1 ? 8 : IsBlockCompressed ? 16 : 0; } }
        /// <summary>Levels of a full chain from Width x Height down to 1x1.</summary>
        public int FullChainLength { get { return BlpDecoder.FullChainLength(Width, Height); } }
    }

    public static class BlpDecoder
    {
        const int HeaderSize = 0x494;

        /// <summary>The header and the stored mip levels, without decoding anything. Throws
        /// WowParseException for what Decode would reject for level 0 (short file, magic, version,
        /// dimensions, an unknown encoding, level 0 absent, too small for its format, or outside the
        /// file) and for a later stored level that is outside the file or too small for its format.</summary>
        public static BlpHeader ReadHeader(byte[] file)
        {
            if (file == null || file.Length < HeaderSize)
                throw new WowParseException(string.Format(
                    "blp: asset is {0} bytes, smaller than the {1}-byte header", file == null ? 0 : file.Length, HeaderSize));

            var c = new ByteCursor(file, "blp");
            string magic = c.ReadMagic();
            if (magic != "BLP2")
                throw new WowParseException("blp: expected a BLP2 header, found '" + magic + "'");
            uint version = c.ReadUInt32();
            if (version != 1)
                throw new WowParseException("blp: unsupported BLP2 version " + version);

            var h = new BlpHeader
            {
                ColorEncoding = c.ReadByte(),
                AlphaDepth = c.ReadByte(),
                PreferredFormat = c.ReadByte(),
                HasMipsRaw = c.ReadByte(),
            };
            h.Width = (int)c.ReadUInt32();
            h.Height = (int)c.ReadUInt32();
            if (h.Width <= 0 || h.Height <= 0 || h.Width > 16384 || h.Height > 16384)
                throw new WowParseException(string.Format("blp: implausible dimensions {0}x{1}", h.Width, h.Height));
            switch (h.ColorEncoding)
            {
                case 1:
                    h.Format = BlpPixelFormat.Palettized;
                    h.Encoding = "palettized/a" + h.AlphaDepth;
                    break;
                case 2:
                    h.Encoding = DxtName((byte)h.PreferredFormat, (byte)h.AlphaDepth);
                    h.Format = h.Encoding == "dxt1" ? BlpPixelFormat.Dxt1 : h.Encoding == "dxt3" ? BlpPixelFormat.Dxt3 : BlpPixelFormat.Dxt5;
                    break;
                case 3:
                    h.Format = BlpPixelFormat.RawBgra;
                    h.Encoding = "bgra8888";
                    break;
                default:
                    throw new WowParseException(string.Format(
                        "blp: unsupported colorEncoding {0} (alphaSize {1}, preferredFormat {2}) -- " +
                        "only palettized (1), DXT (2) and raw BGRA (3) are implemented",
                        h.ColorEncoding, h.AlphaDepth, h.PreferredFormat));
            }

            var offsets = new uint[16];
            var sizes = new uint[16];
            for (int i = 0; i < 16; i++) offsets[i] = c.ReadUInt32();
            for (int i = 0; i < 16; i++) sizes[i] = c.ReadUInt32();
            int levels = 0;
            int full = FullChainLength(h.Width, h.Height);
            while (levels < 16 && levels < full && sizes[levels] != 0)
                levels++;
            if (levels == 0)
                throw new WowParseException("blp: mip level 0 is not present");

            h.Mips = new BlpMipLevel[levels];
            for (int m = 0; m < levels; m++)
            {
                var level = new BlpMipLevel
                {
                    Level = m,
                    Width = Math.Max(1, h.Width >> m),
                    Height = Math.Max(1, h.Height >> m),
                    Offset = (int)offsets[m],
                    StoredSize = (int)sizes[m],
                };
                if (offsets[m] > int.MaxValue || sizes[m] > int.MaxValue)
                    throw new WowParseException(string.Format("blp: mip {0} has offset {1} and size {2}, beyond any file", m, offsets[m], sizes[m]));
                c.Require(level.Offset, level.StoredSize);
                long pixels = (long)level.Width * level.Height;
                long minimum;
                switch (h.Format)
                {
                    case BlpPixelFormat.Palettized:
                        long alphaBytes = h.AlphaDepth == 8 ? pixels : h.AlphaDepth == 4 ? (pixels + 1) / 2 : h.AlphaDepth == 1 ? (pixels + 7) / 8 : 0;
                        level.DataSize = (int)(pixels + alphaBytes);
                        minimum = pixels;
                        c.Require(level.Offset, level.DataSize);
                        break;
                    case BlpPixelFormat.RawBgra:
                        level.DataSize = (int)(pixels * 4);
                        minimum = level.DataSize;
                        break;
                    default:
                        level.DataSize = Math.Max(1, (level.Width + 3) / 4) * Math.Max(1, (level.Height + 3) / 4) * h.BlockBytes;
                        minimum = level.DataSize;
                        break;
                }
                if (level.StoredSize < minimum)
                    throw new WowParseException(string.Format(
                        "blp: {0} mip {1} holds {2} bytes, needs {3} for {4}x{5}", h.Encoding, m, level.StoredSize, minimum, level.Width, level.Height));
                h.Mips[m] = level;
            }
            return h;
        }

        /// <summary>Levels of a full mip chain from width x height down to 1x1: floor(log2(max)) + 1.</summary>
        public static int FullChainLength(int width, int height)
        {
            int n = 1;
            for (int s = Math.Max(width, height); s > 1; s >>= 1)
                n++;
            return n;
        }

        /// <summary>Decode one mip level (default: the largest) to RGBA32. `exactBlocks` applies to DXT only
        /// (palettized and raw BGRA are exact either way): false (the default) keeps the truncating decode the
        /// model paths use, true selects the block format's own decode rounded to the nearest 8-bit value
        /// (see the file header, DXT DECODE MODES).</summary>
        public static BlpImage Decode(byte[] file, int mipLevel = 0, bool exactBlocks = false)
        {
            if (file == null || file.Length < HeaderSize)
                throw new WowParseException(string.Format(
                    "blp: asset is {0} bytes, smaller than the {1}-byte header", file == null ? 0 : file.Length, HeaderSize));

            var c = new ByteCursor(file, "blp");
            string magic = c.ReadMagic();
            if (magic != "BLP2")
                throw new WowParseException("blp: expected a BLP2 header, found '" + magic + "'");
            uint version = c.ReadUInt32();
            if (version != 1)
                throw new WowParseException("blp: unsupported BLP2 version " + version);

            byte colorEncoding = c.ReadByte();
            byte alphaSize = c.ReadByte();
            byte preferredFormat = c.ReadByte();
            c.ReadByte();  // hasMips
            int width = (int)c.ReadUInt32();
            int height = (int)c.ReadUInt32();
            if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
                throw new WowParseException(string.Format("blp: implausible dimensions {0}x{1}", width, height));

            var mipOffsets = new uint[16];
            var mipSizes = new uint[16];
            for (int i = 0; i < 16; i++) mipOffsets[i] = c.ReadUInt32();
            for (int i = 0; i < 16; i++) mipSizes[i] = c.ReadUInt32();

            if (mipLevel < 0 || mipLevel > 15 || mipSizes[mipLevel] == 0)
                throw new WowParseException("blp: mip level " + mipLevel + " is not present");

            int mipWidth = Math.Max(1, width >> mipLevel);
            int mipHeight = Math.Max(1, height >> mipLevel);
            int dataOffset = (int)mipOffsets[mipLevel];
            int dataSize = (int)mipSizes[mipLevel];
            c.Require(dataOffset, dataSize);

            var img = new BlpImage
            {
                Width = mipWidth,
                Height = mipHeight,
                MipLevel = mipLevel,
                Rgba = new byte[mipWidth * mipHeight * 4],
                HasAlpha = alphaSize > 0,
            };

            switch (colorEncoding)
            {
                case 1:
                    img.Encoding = "palettized/a" + alphaSize;
                    DecodePalettized(file, c, dataOffset, dataSize, alphaSize, img);
                    break;
                case 2:
                    img.Encoding = DxtName(preferredFormat, alphaSize);
                    DecodeDxt(file, c, dataOffset, dataSize, preferredFormat, alphaSize, exactBlocks, img);
                    break;
                case 3:
                    img.Encoding = "bgra8888";
                    DecodeRawBgra(file, c, dataOffset, dataSize, img);
                    break;
                default:
                    throw new WowParseException(string.Format(
                        "blp: unsupported colorEncoding {0} (alphaSize {1}, preferredFormat {2}) -- " +
                        "only palettized (1), DXT (2) and raw BGRA (3) are implemented",
                        colorEncoding, alphaSize, preferredFormat));
            }
            return img;
        }

        static string DxtName(byte preferredFormat, byte alphaSize)
        {
            if (preferredFormat == 0 || alphaSize <= 1) return "dxt1";
            if (preferredFormat == 1) return "dxt3";
            return "dxt5";
        }

        // ---------------------------------------------------------------- palettized

        static void DecodePalettized(byte[] file, ByteCursor c, int dataOffset, int dataSize,
                                     byte alphaSize, BlpImage img)
        {
            int pixels = img.Width * img.Height;
            if (dataSize < pixels)
                throw new WowParseException(string.Format(
                    "blp: palettized mip holds {0} bytes for {1} pixels", dataSize, pixels));

            // palette entries are BGRA
            const int PaletteOffset = 0x94;
            c.Require(PaletteOffset, 256 * 4);

            int alphaOffset = dataOffset + pixels;
            int alphaBytes = alphaSize == 8 ? pixels
                           : alphaSize == 4 ? (pixels + 1) / 2
                           : alphaSize == 1 ? (pixels + 7) / 8
                           : 0;
            if (alphaBytes > 0)
                c.Require(alphaOffset, alphaBytes);

            for (int i = 0; i < pixels; i++)
            {
                int p = file[dataOffset + i] * 4;
                img.Rgba[i * 4 + 0] = file[PaletteOffset + p + 2];  // R  (palette is BGRA)
                img.Rgba[i * 4 + 1] = file[PaletteOffset + p + 1];  // G
                img.Rgba[i * 4 + 2] = file[PaletteOffset + p + 0];  // B

                byte a = 255;
                switch (alphaSize)
                {
                    case 8: a = file[alphaOffset + i]; break;
                    case 4:
                    {
                        byte both = file[alphaOffset + (i >> 1)];
                        int nibble = ((i & 1) == 0) ? (both & 0x0F) : (both >> 4);
                        a = (byte)(nibble * 17);   // 0..15 -> 0..255
                        break;
                    }
                    case 1:
                    {
                        byte bits = file[alphaOffset + (i >> 3)];
                        a = (byte)(((bits >> (i & 7)) & 1) != 0 ? 255 : 0);
                        break;
                    }
                }
                img.Rgba[i * 4 + 3] = a;
            }
        }

        // ---------------------------------------------------------------- raw BGRA

        static void DecodeRawBgra(byte[] file, ByteCursor c, int dataOffset, int dataSize, BlpImage img)
        {
            int pixels = img.Width * img.Height;
            if (dataSize < pixels * 4)
                throw new WowParseException(string.Format(
                    "blp: raw mip holds {0} bytes for {1} pixels", dataSize, pixels));
            for (int i = 0; i < pixels; i++)
            {
                img.Rgba[i * 4 + 0] = file[dataOffset + i * 4 + 2];
                img.Rgba[i * 4 + 1] = file[dataOffset + i * 4 + 1];
                img.Rgba[i * 4 + 2] = file[dataOffset + i * 4 + 0];
                img.Rgba[i * 4 + 3] = file[dataOffset + i * 4 + 3];
            }
        }

        // ---------------------------------------------------------------- DXT

        static void DecodeDxt(byte[] file, ByteCursor c, int dataOffset, int dataSize,
                              byte preferredFormat, byte alphaSize, bool exact, BlpImage img)
        {
            string kind = DxtName(preferredFormat, alphaSize);
            int blockBytes = (kind == "dxt1") ? 8 : 16;
            int blocksX = Math.Max(1, (img.Width + 3) / 4);
            int blocksY = Math.Max(1, (img.Height + 3) / 4);
            long needed = (long)blocksX * blocksY * blockBytes;
            if (dataSize < needed)
                throw new WowParseException(string.Format(
                    "blp: {0} mip holds {1} bytes, needs {2} for {3}x{4}", kind, dataSize, needed, img.Width, img.Height));

            var color = new byte[16 * 4];
            for (int by = 0; by < blocksY; by++)
            {
                for (int bx = 0; bx < blocksX; bx++)
                {
                    int block = dataOffset + (by * blocksX + bx) * blockBytes;
                    int colorBlock = (kind == "dxt1") ? block : block + 8;
                    DecodeColorBlock(file, colorBlock, kind == "dxt1", exact, color);

                    if (kind == "dxt3") DecodeAlphaBlockExplicit(file, block, color);
                    else if (kind == "dxt5") DecodeAlphaBlockInterpolated(file, block, exact, color);

                    for (int py = 0; py < 4; py++)
                    {
                        int y = by * 4 + py;
                        if (y >= img.Height) break;
                        for (int px = 0; px < 4; px++)
                        {
                            int x = bx * 4 + px;
                            if (x >= img.Width) break;
                            int src = (py * 4 + px) * 4;
                            int dst = (y * img.Width + x) * 4;
                            img.Rgba[dst + 0] = color[src + 0];
                            img.Rgba[dst + 1] = color[src + 1];
                            img.Rgba[dst + 2] = color[src + 2];
                            img.Rgba[dst + 3] = color[src + 3];
                        }
                    }
                }
            }
        }

        static void DecodeColorBlock(byte[] f, int o, bool dxt1, bool exact, byte[] outRgba)
        {
            ushort c0 = (ushort)(f[o] | (f[o + 1] << 8));
            ushort c1 = (ushort)(f[o + 2] | (f[o + 3] << 8));
            uint bits = (uint)(f[o + 4] | (f[o + 5] << 8) | (f[o + 6] << 16) | (f[o + 7] << 24));

            var r = new byte[4]; var g = new byte[4]; var b = new byte[4]; var a = new byte[4];
            if (exact)
            {
                Rgb565Replicated(c0, out r[0], out g[0], out b[0]); a[0] = 255;
                Rgb565Replicated(c1, out r[1], out g[1], out b[1]); a[1] = 255;
            }
            else
            {
                Rgb565(c0, out r[0], out g[0], out b[0]); a[0] = 255;
                Rgb565(c1, out r[1], out g[1], out b[1]); a[1] = 255;
            }

            if (!dxt1 || c0 > c1)
            {
                if (exact)
                {
                    r[2] = Nearest(2 * r[0] + r[1], 3); g[2] = Nearest(2 * g[0] + g[1], 3); b[2] = Nearest(2 * b[0] + b[1], 3); a[2] = 255;
                    r[3] = Nearest(r[0] + 2 * r[1], 3); g[3] = Nearest(g[0] + 2 * g[1], 3); b[3] = Nearest(b[0] + 2 * b[1], 3); a[3] = 255;
                }
                else
                {
                    r[2] = (byte)((2 * r[0] + r[1]) / 3); g[2] = (byte)((2 * g[0] + g[1]) / 3); b[2] = (byte)((2 * b[0] + b[1]) / 3); a[2] = 255;
                    r[3] = (byte)((r[0] + 2 * r[1]) / 3); g[3] = (byte)((g[0] + 2 * g[1]) / 3); b[3] = (byte)((b[0] + 2 * b[1]) / 3); a[3] = 255;
                }
            }
            else
            {
                if (exact)
                {
                    r[2] = Nearest(r[0] + r[1], 2); g[2] = Nearest(g[0] + g[1], 2); b[2] = Nearest(b[0] + b[1], 2); a[2] = 255;
                }
                else
                {
                    r[2] = (byte)((r[0] + r[1]) / 2); g[2] = (byte)((g[0] + g[1]) / 2); b[2] = (byte)((b[0] + b[1]) / 2); a[2] = 255;
                }
                r[3] = 0; g[3] = 0; b[3] = 0; a[3] = 0;   // punch-through transparent
            }

            for (int i = 0; i < 16; i++)
            {
                int sel = (int)((bits >> (i * 2)) & 3);
                outRgba[i * 4 + 0] = r[sel];
                outRgba[i * 4 + 1] = g[sel];
                outRgba[i * 4 + 2] = b[sel];
                outRgba[i * 4 + 3] = a[sel];
            }
        }

        static void Rgb565(ushort v, out byte r, out byte g, out byte b)
        {
            r = (byte)(((v >> 11) & 0x1F) * 255 / 31);
            g = (byte)(((v >> 5) & 0x3F) * 255 / 63);
            b = (byte)((v & 0x1F) * 255 / 31);
        }

        /// <summary>A 565 colour expanded to 8 bits by bit replication (the exact-blocks mode).</summary>
        static void Rgb565Replicated(ushort v, out byte r, out byte g, out byte b)
        {
            int r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
            r = (byte)((r5 << 3) | (r5 >> 2));
            g = (byte)((g6 << 2) | (g6 >> 4));
            b = (byte)((b5 << 3) | (b5 >> 2));
        }

        /// <summary>n / d (n >= 0, d > 0) rounded to the nearest integer, ties to even. For d = 3, 5 and 7 no
        /// tie occurs; for d = 2 an odd n is a tie.</summary>
        static byte Nearest(int n, int d)
        {
            int q = n / d, rem = n - q * d;
            if (2 * rem > d || (2 * rem == d && (q & 1) != 0))
                q++;
            return (byte)q;
        }

        static void DecodeAlphaBlockExplicit(byte[] f, int o, byte[] outRgba)   // DXT3
        {
            for (int i = 0; i < 16; i++)
            {
                byte both = f[o + (i >> 1)];
                int nibble = ((i & 1) == 0) ? (both & 0x0F) : (both >> 4);
                outRgba[i * 4 + 3] = (byte)(nibble * 17);
            }
        }

        static void DecodeAlphaBlockInterpolated(byte[] f, int o, bool exact, byte[] outRgba)   // DXT5
        {
            var a = new byte[8];
            a[0] = f[o]; a[1] = f[o + 1];
            if (a[0] > a[1])
                for (int i = 0; i < 6; i++)
                    a[2 + i] = exact ? Nearest((6 - i) * a[0] + (1 + i) * a[1], 7) : (byte)(((6 - i) * a[0] + (1 + i) * a[1]) / 7);
            else
            {
                for (int i = 0; i < 4; i++)
                    a[2 + i] = exact ? Nearest((4 - i) * a[0] + (1 + i) * a[1], 5) : (byte)(((4 - i) * a[0] + (1 + i) * a[1]) / 5);
                a[6] = 0; a[7] = 255;
            }

            ulong bits = 0;
            for (int i = 0; i < 6; i++) bits |= (ulong)f[o + 2 + i] << (8 * i);
            for (int i = 0; i < 16; i++)
                outRgba[i * 4 + 3] = a[(int)((bits >> (i * 3)) & 7)];
        }
    }
}
