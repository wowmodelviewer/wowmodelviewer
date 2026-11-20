#include "BlockDecode.h"

namespace
{
  inline uint16_t read16(const uint8_t * p) { return (uint16_t)(p[0] | (p[1] << 8)); }
  inline uint32_t read32(const uint8_t * p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

  void expand565(uint16_t v, uint8_t out[4])
  {
    const unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[0] = (uint8_t)((r << 3) | (r >> 2));
    out[1] = (uint8_t)((g << 2) | (g >> 4));
    out[2] = (uint8_t)((b << 3) | (b >> 2));
    out[3] = 255;
  }

  // The colour half of a block into a 4x4 RGBA tile. fourColour: always the 4-colour palette (DXT3/5 blocks).
  void colourBlock(const uint8_t * b, bool fourColour, bool punchThroughAlpha, uint8_t tile[16][4])
  {
    const uint16_t c0 = read16(b), c1 = read16(b + 2);
    uint8_t pal[4][4];
    expand565(c0, pal[0]);
    expand565(c1, pal[1]);
    if (fourColour || c0 > c1)
    {
      for (int k = 0; k < 3; k++)
      {
        pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k] + 1) / 3);
        pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
      }
      pal[2][3] = pal[3][3] = 255;
    }
    else
    {
      for (int k = 0; k < 3; k++)
      {
        pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
        pal[3][k] = 0;
      }
      pal[2][3] = 255;
      pal[3][3] = punchThroughAlpha ? 0 : 255;
    }
    const uint32_t idx = read32(b + 4);
    for (int i = 0; i < 16; i++)
    {
      const uint8_t * c = pal[(idx >> (2 * i)) & 3];
      tile[i][0] = c[0]; tile[i][1] = c[1]; tile[i][2] = c[2]; tile[i][3] = c[3];
    }
  }

  void explicitAlpha(const uint8_t * b, uint8_t tile[16][4])
  {
    for (int i = 0; i < 16; i++)
    {
      const unsigned nibble = (b[i / 2] >> ((i & 1) * 4)) & 15;
      tile[i][3] = (uint8_t)(nibble * 17);
    }
  }

  void interpolatedAlpha(const uint8_t * b, uint8_t tile[16][4])
  {
    const unsigned a0 = b[0], a1 = b[1];
    uint8_t pal[8];
    pal[0] = (uint8_t)a0;
    pal[1] = (uint8_t)a1;
    if (a0 > a1)
      for (int i = 1; i < 7; i++)
        pal[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1 + 3) / 7);
    else
    {
      for (int i = 1; i < 5; i++)
        pal[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1 + 2) / 5);
      pal[6] = 0;
      pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int k = 0; k < 6; k++)
      bits |= (uint64_t)b[2 + k] << (8 * k);
    for (int i = 0; i < 16; i++)
      tile[i][3] = pal[(bits >> (3 * i)) & 7];
  }
}

void wmv::decodeBlocks(BlockFormat format, const uint8_t * src, unsigned width, unsigned height, uint8_t * dest)
{
  const unsigned bw = (width + 3) / 4, bh = (height + 3) / 4;
  const size_t stride = blockBytes(format);
  uint8_t tile[16][4];
  for (unsigned by = 0; by < bh; by++)
    for (unsigned bx = 0; bx < bw; bx++, src += stride)
    {
      switch (format)
      {
      case BLOCK_DXT1_RGB:  colourBlock(src, false, false, tile); break;
      case BLOCK_DXT1_RGBA: colourBlock(src, false, true, tile); break;
      case BLOCK_DXT3:      colourBlock(src + 8, true, false, tile); explicitAlpha(src, tile); break;
      case BLOCK_DXT5:      colourBlock(src + 8, true, false, tile); interpolatedAlpha(src, tile); break;
      }
      for (unsigned y = 0; y < 4; y++)
      {
        const unsigned py = by * 4 + y;
        if (py >= height)
          break;
        for (unsigned x = 0; x < 4; x++)
        {
          const unsigned px = bx * 4 + x;
          if (px >= width)
            break;
          uint8_t * d = dest + ((size_t)py * width + px) * 4;
          const uint8_t * t = tile[y * 4 + x];
          d[0] = t[0]; d[1] = t[1]; d[2] = t[2]; d[3] = t[3];
        }
      }
    }
}
