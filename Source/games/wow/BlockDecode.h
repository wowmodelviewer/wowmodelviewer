/*
 * BlockDecode.h
 *
 * The S3TC / BCn block formats BLP textures use, decoded to RGBA8 on the CPU exactly as a GPU's hardware decoder
 * does (the DXT1, DXT3 and DXT5 rules of EXT_texture_compression_s3tc). The viewer used to have the graphics
 * driver do this; ddslib's old software path got DXT5 alpha and DXT3/5 colours wrong.
 */

#ifndef _BLOCKDECODE_H_
#define _BLOCKDECODE_H_

#include <cstddef>
#include <cstdint>

namespace wmv
{
  enum BlockFormat
  {
    BLOCK_DXT1_RGB,   // BC1 without alpha: the fourth colour of a 3-colour block is opaque black
    BLOCK_DXT1_RGBA,  // BC1 with 1-bit alpha: that colour is transparent black
    BLOCK_DXT3,       // BC2: explicit 4-bit alpha
    BLOCK_DXT5        // BC3: interpolated alpha
  };

  // Bytes one 4x4 block of the format takes.
  inline size_t blockBytes(BlockFormat f) { return (f == BLOCK_DXT1_RGB || f == BLOCK_DXT1_RGBA) ? 8 : 16; }

  // Decodes a width x height level (any size: edge blocks are cropped) from src into dest, RGBA8, top row first.
  // src must hold ceil(w/4) * ceil(h/4) blocks.
  void decodeBlocks(BlockFormat format, const uint8_t * src, unsigned width, unsigned height, uint8_t * dest);
}

#endif
