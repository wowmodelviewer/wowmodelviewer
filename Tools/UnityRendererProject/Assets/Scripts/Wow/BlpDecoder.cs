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
    }

    public static class BlpDecoder
    {
        const int HeaderSize = 0x494;

        /// <summary>Decode one mip level (default: the largest) to RGBA32.</summary>
        public static BlpImage Decode(byte[] file, int mipLevel = 0)
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
                    DecodeDxt(file, c, dataOffset, dataSize, preferredFormat, alphaSize, img);
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
                              byte preferredFormat, byte alphaSize, BlpImage img)
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
                    DecodeColorBlock(file, colorBlock, kind == "dxt1", color);

                    if (kind == "dxt3") DecodeAlphaBlockExplicit(file, block, color);
                    else if (kind == "dxt5") DecodeAlphaBlockInterpolated(file, block, color);

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

        static void DecodeColorBlock(byte[] f, int o, bool dxt1, byte[] outRgba)
        {
            ushort c0 = (ushort)(f[o] | (f[o + 1] << 8));
            ushort c1 = (ushort)(f[o + 2] | (f[o + 3] << 8));
            uint bits = (uint)(f[o + 4] | (f[o + 5] << 8) | (f[o + 6] << 16) | (f[o + 7] << 24));

            var r = new byte[4]; var g = new byte[4]; var b = new byte[4]; var a = new byte[4];
            Rgb565(c0, out r[0], out g[0], out b[0]); a[0] = 255;
            Rgb565(c1, out r[1], out g[1], out b[1]); a[1] = 255;

            if (!dxt1 || c0 > c1)
            {
                r[2] = (byte)((2 * r[0] + r[1]) / 3); g[2] = (byte)((2 * g[0] + g[1]) / 3); b[2] = (byte)((2 * b[0] + b[1]) / 3); a[2] = 255;
                r[3] = (byte)((r[0] + 2 * r[1]) / 3); g[3] = (byte)((g[0] + 2 * g[1]) / 3); b[3] = (byte)((b[0] + 2 * b[1]) / 3); a[3] = 255;
            }
            else
            {
                r[2] = (byte)((r[0] + r[1]) / 2); g[2] = (byte)((g[0] + g[1]) / 2); b[2] = (byte)((b[0] + b[1]) / 2); a[2] = 255;
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

        static void DecodeAlphaBlockExplicit(byte[] f, int o, byte[] outRgba)   // DXT3
        {
            for (int i = 0; i < 16; i++)
            {
                byte both = f[o + (i >> 1)];
                int nibble = ((i & 1) == 0) ? (both & 0x0F) : (both >> 4);
                outRgba[i * 4 + 3] = (byte)(nibble * 17);
            }
        }

        static void DecodeAlphaBlockInterpolated(byte[] f, int o, byte[] outRgba)   // DXT5
        {
            var a = new byte[8];
            a[0] = f[o]; a[1] = f[o + 1];
            if (a[0] > a[1])
                for (int i = 0; i < 6; i++) a[2 + i] = (byte)(((6 - i) * a[0] + (1 + i) * a[1]) / 7);
            else
            {
                for (int i = 0; i < 4; i++) a[2 + i] = (byte)(((4 - i) * a[0] + (1 + i) * a[1]) / 5);
                a[6] = 0; a[7] = 255;
            }

            ulong bits = 0;
            for (int i = 0; i < 6; i++) bits |= (ulong)f[o + 2 + i] << (8 * i);
            for (int i = 0; i < 16; i++)
                outRgba[i * 4 + 3] = a[(int)((bits >> (i * 3)) & 7)];
        }
    }
}
