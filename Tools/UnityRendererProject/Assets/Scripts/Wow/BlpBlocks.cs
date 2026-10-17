// BlpBlocks.cs
//
// A BLP2 file's DXT1/3/5 levels as the GPU takes them, for the world's block-compressed uploads
// (docs/world-viewer/08-streaming.md 1.9). The blocks of BC1/BC2/BC3 are the ones the file stores, byte for byte
// (04-terrain-materials.md: endpoints exact, every interpolant the GPU's). What changes is the row order: the model and
// world-model paths flip V twice -- once in their UVs (WowCoordinateConverter.ConvertTexCoord: v = 1 - y) and once in the
// pixel rows of the RGBA32 upload (WmvModelBuilder.CreateTexture) -- so a block upload for those paths must hold the
// image upside down too. That is exact on blocks: the block rows are reversed, and inside each block its pixel rows
// (DXT1/the colour half: one index byte per row; DXT3 alpha: one 16-bit word per row; DXT5 alpha: one 12-bit field of
// the 48-bit index per row). A level shorter than a block (height 2 or 1) holds its rows at the top of its one block row;
// they are reversed among themselves.
//
// Only a file every level of which is whole blocks (or shorter than a block) and that stores its full mip chain is
// uploaded this way: a level of height 6 holds its last two rows in a half-filled block row, and moving that row to the
// top would put padding at the top of the image; a file without its mips would lose them (Unity cannot generate the
// mips of a compressed format). Unsupported says which rule a file breaks.

using System;

namespace Wmv.Wow
{
    /// <summary>The stored levels of a DXT file, each flipped for the model convention, back to back.</summary>
    public sealed class BlpBlockChain
    {
        public BlpPixelFormat Format;
        public int Width, Height;
        public int Levels;
        public byte[] Data;
        public int[] LevelOffsets;   // where level m starts in Data
        public int[] LevelSizes;     // its bytes (whole blocks, D3D/Unity layout: one block at the least)
        public long Bytes { get { return Data == null ? 0 : Data.LongLength; } }
    }

    public static class BlpBlocks
    {
        /// <summary>Null when the file's stored levels can go up as their own blocks for the model convention, else why
        /// not.</summary>
        public static string Unsupported(BlpHeader h)
        {
            if (h == null)
                return "no header";
            if (!h.IsBlockCompressed)
                return h.Encoding + " is not block-compressed";
            if (h.Width % 4 != 0 || h.Height % 4 != 0)
                return string.Format("{0}x{1} is not whole 4x4 blocks", h.Width, h.Height);
            if (h.MipCount < h.FullChainLength)
                return string.Format("{0} of the {1} levels of a full mip chain are stored", h.MipCount, h.FullChainLength);
            for (int m = 0; m < h.MipCount; m++)
            {
                int lh = h.Mips[m].Height;
                if (lh >= 4 && lh % 4 != 0)
                    return string.Format("level {0} is {1} pixels high: its last block row is part padding", m, lh);
            }
            return null;
        }

        /// <summary>The file's stored levels flipped for the model convention (Unsupported(h) must be null).</summary>
        public static BlpBlockChain FlippedChain(byte[] file, BlpHeader h)
        {
            string why = Unsupported(h);
            if (why != null)
                throw new ArgumentException("blp blocks: " + why);
            int n = h.MipCount;
            var chain = new BlpBlockChain
            {
                Format = h.Format, Width = h.Width, Height = h.Height, Levels = n,
                LevelOffsets = new int[n], LevelSizes = new int[n],
            };
            long total = 0;
            for (int m = 0; m < n; m++)
                total += h.Mips[m].DataSize;
            chain.Data = new byte[total];
            int at = 0;
            for (int m = 0; m < n; m++)
            {
                BlpMipLevel level = h.Mips[m];
                chain.LevelOffsets[m] = at;
                chain.LevelSizes[m] = level.DataSize;
                FlipLevel(file, level.Offset, chain.Data, at, level.Width, level.Height, h.Format);
                at += level.DataSize;
            }
            return chain;
        }

        /// <summary>
        /// A texture of the world's models, made ready on a worker: its own blocks, flipped for the model convention, when the
        /// file qualifies (Unsupported) and the GPU samples its format (gpuSamples); otherwise decoded to RGBA32 as before
        /// (BlpDecoder.Decode, the models' decode), with BlocksNotUsed saying why. Throws WowParseException for a file the
        /// decoder refuses.
        /// </summary>
        public static BlpImage PrepareForModels(byte[] file, Func<BlpPixelFormat, bool> gpuSamples)
        {
            BlpHeader h;
            try { h = BlpDecoder.ReadHeader(file); }
            catch (WowParseException e)
            {
                // A stored level the header's table names but the file holds too little of (a small mip shorter than its
                // size needs, seen in Retail M2 textures): level 0 decoded as before Stage 08 (its mips generated), the
                // reason kept. A level 0 that cannot be decoded throws, as it did.
                BlpImage d = BlpDecoder.Decode(file);
                d.BlocksNotUsed = "its mip table: " + e.Message;
                return d;
            }
            string why = Unsupported(h);
            if (why == null && (gpuSamples == null || !gpuSamples(h.Format)))
                why = "this GPU does not sample " + h.Encoding;
            if (why == null)
                return new BlpImage
                {
                    Width = h.Width, Height = h.Height, MipLevel = 0, Rgba = null, Encoding = h.Encoding, HasAlpha = h.AlphaDepth > 0,
                    Blocks = FlippedChain(file, h),
                };
            BlpImage img = BlpDecoder.Decode(file);
            img.BlocksNotUsed = why;
            return img;
        }

        /// <summary>The bytes of an RGBA32 upload of width x height with its full mip chain (what Apply(true) makes).</summary>
        public static long Rgba32ChainBytes(int width, int height)
        {
            long b = 0;
            int w = width, h = height;
            for (; ; )
            {
                b += (long)w * h * 4;
                if (w == 1 && h == 1)
                    break;
                w = Math.Max(1, w >> 1);
                h = Math.Max(1, h >> 1);
            }
            return b;
        }

        /// <summary>The GPU bytes of an image's upload: its blocks as stored, or RGBA32 with the full chain.</summary>
        public static long UploadBytes(BlpImage image)
        {
            if (image == null)
                return 0;
            return image.Blocks != null ? image.Blocks.Bytes : Rgba32ChainBytes(image.Width, image.Height);
        }

        public static int BlockBytes(BlpPixelFormat f)
        {
            return f == BlpPixelFormat.Dxt1 ? 8 : 16;
        }

        /// <summary>One level of width x height pixels from src at srcOffset into dst at dstOffset, upside down.</summary>
        public static void FlipLevel(byte[] src, int srcOffset, byte[] dst, int dstOffset, int width, int height, BlpPixelFormat f)
        {
            int bb = BlockBytes(f);
            int bx = Math.Max(1, (width + 3) / 4), by = Math.Max(1, (height + 3) / 4);
            int rows = height >= 4 ? 4 : height;   // the rows of a block that are pixels of the image
            for (int y = 0; y < by; y++)
            {
                int ty = by - 1 - y;
                for (int x = 0; x < bx; x++)
                {
                    int s = srcOffset + (y * bx + x) * bb;
                    int d = dstOffset + (ty * bx + x) * bb;
                    Buffer.BlockCopy(src, s, dst, d, bb);
                    FlipBlock(dst, d, f, rows);
                }
            }
        }

        /// <summary>The first `rows` pixel rows of the block at o reversed among themselves.</summary>
        public static void FlipBlock(byte[] b, int o, BlpPixelFormat f, int rows)
        {
            if (rows <= 1)
                return;
            int colour = f == BlpPixelFormat.Dxt1 ? o : o + 8;
            for (int i = 0, j = rows - 1; i < j; i++, j--)
            {
                // colour indices: one byte per row after the two 16-bit endpoints
                byte t = b[colour + 4 + i];
                b[colour + 4 + i] = b[colour + 4 + j];
                b[colour + 4 + j] = t;
                if (f == BlpPixelFormat.Dxt3)
                {
                    // explicit alpha: one 16-bit word per row
                    for (int k = 0; k < 2; k++)
                    {
                        byte u = b[o + 2 * i + k];
                        b[o + 2 * i + k] = b[o + 2 * j + k];
                        b[o + 2 * j + k] = u;
                    }
                }
            }
            if (f == BlpPixelFormat.Dxt5)
            {
                // interpolated alpha: after the two endpoints a 48-bit little-endian field of 3-bit indices, row r in bits
                // 12r..12r+11
                ulong bits = 0;
                for (int k = 0; k < 6; k++)
                    bits |= (ulong)b[o + 2 + k] << (8 * k);
                var row = new ulong[4];
                for (int r = 0; r < 4; r++)
                    row[r] = (bits >> (12 * r)) & 0xFFFUL;
                for (int i = 0, j = rows - 1; i < j; i++, j--)
                {
                    ulong t = row[i];
                    row[i] = row[j];
                    row[j] = t;
                }
                bits = 0;
                for (int r = 0; r < 4; r++)
                    bits |= row[r] << (12 * r);
                for (int k = 0; k < 6; k++)
                    b[o + 2 + k] = (byte)(bits >> (8 * k));
            }
        }
    }
}
