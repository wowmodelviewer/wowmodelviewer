/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
| Minimal, self-contained OpenEXR writer (no external dependency).       |
\*----------------------------------------------------------------------*/

#include "MiniExr.h"

#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

namespace
{
  // Little-endian writers (EXR is little-endian).
  inline void w8 (std::vector<unsigned char> & b, uint8_t v)  { b.push_back(v); }
  inline void w32(std::vector<unsigned char> & b, uint32_t v) { b.push_back(v & 0xff); b.push_back((v>>8)&0xff); b.push_back((v>>16)&0xff); b.push_back((v>>24)&0xff); }
  inline void w64(std::vector<unsigned char> & b, uint64_t v) { for (int i = 0; i < 8; i++) b.push_back((unsigned char)((v >> (8*i)) & 0xff)); }
  inline void wf (std::vector<unsigned char> & b, float f)    { uint32_t u; std::memcpy(&u, &f, 4); w32(b, u); }
  inline void wstr(std::vector<unsigned char> & b, const char * s) { while (*s) b.push_back((unsigned char)*s++); b.push_back(0); }

  void writeAttr(std::vector<unsigned char> & h, const char * name, const char * type, const std::vector<unsigned char> & val)
  {
    wstr(h, name);
    wstr(h, type);
    w32(h, (uint32_t)val.size());
    h.insert(h.end(), val.begin(), val.end());
  }
}

namespace MiniExr
{
  bool writeRGBAF(const std::wstring & path, int width, int height, const float * rgba, bool bottomUp)
  {
    if (width <= 0 || height <= 0 || !rgba)
      return false;

    std::vector<unsigned char> header;

    // --- channels (alphabetical: A, B, G, R), all FLOAT (pixelType 2) ---
    {
      std::vector<unsigned char> ch;
      const char * names[4] = { "A", "B", "G", "R" };
      for (int c = 0; c < 4; c++)
      {
        wstr(ch, names[c]);
        w32(ch, 2);   // pixelType: 2 = FLOAT
        w8 (ch, 0);   // pLinear
        w8 (ch, 0); w8(ch, 0); w8(ch, 0); // reserved
        w32(ch, 1);   // xSampling
        w32(ch, 1);   // ySampling
      }
      ch.push_back(0); // end of channel list
      writeAttr(header, "channels", "chlist", ch);
    }
    // --- compression: 0 = NO_COMPRESSION ---
    { std::vector<unsigned char> v; w8(v, 0); writeAttr(header, "compression", "compression", v); }
    // --- dataWindow / displayWindow ---
    { std::vector<unsigned char> v; w32(v,0); w32(v,0); w32(v,(uint32_t)(width-1)); w32(v,(uint32_t)(height-1)); writeAttr(header, "dataWindow", "box2i", v); }
    { std::vector<unsigned char> v; w32(v,0); w32(v,0); w32(v,(uint32_t)(width-1)); w32(v,(uint32_t)(height-1)); writeAttr(header, "displayWindow", "box2i", v); }
    // --- lineOrder: 0 = INCREASING_Y ---
    { std::vector<unsigned char> v; w8(v, 0); writeAttr(header, "lineOrder", "lineOrder", v); }
    // --- pixelAspectRatio ---
    { std::vector<unsigned char> v; wf(v, 1.0f); writeAttr(header, "pixelAspectRatio", "float", v); }
    // --- screenWindowCenter / Width ---
    { std::vector<unsigned char> v; wf(v, 0.0f); wf(v, 0.0f); writeAttr(header, "screenWindowCenter", "v2f", v); }
    { std::vector<unsigned char> v; wf(v, 1.0f); writeAttr(header, "screenWindowWidth", "float", v); }
    header.push_back(0); // end of header

    // Each scanline block: int32 y + uint32 dataSize + (A,B,G,R rows), W floats each.
    const uint32_t rowBytes = (uint32_t)width * 4u * 4u; // 4 channels * float
    const uint64_t blockBytes = 8ull + rowBytes;

    // 8-byte magic+version, then offset table (one uint64 per scanline).
    const uint64_t magicVer = 8;
    const uint64_t offTableStart = magicVer + header.size();
    const uint64_t pixelStart = offTableStart + (uint64_t)height * 8ull;

    std::vector<unsigned char> offsets;
    for (int y = 0; y < height; y++)
      w64(offsets, pixelStart + (uint64_t)y * blockBytes);

    std::ofstream f(std::string(path.begin(), path.end()).c_str(), std::ios::binary);
    if (!f.is_open())
      return false;

    // magic + version
    const unsigned char magic[4] = { 0x76, 0x2f, 0x31, 0x01 };
    f.write((const char*)magic, 4);
    const unsigned char ver[4] = { 0x02, 0x00, 0x00, 0x00 };
    f.write((const char*)ver, 4);
    f.write((const char*)header.data(), header.size());
    f.write((const char*)offsets.data(), offsets.size());

    // scanlines, top-down (EXR y=0 is top)
    std::vector<float> rowA(width), rowB(width), rowG(width), rowR(width);
    for (int y = 0; y < height; y++)
    {
      const int srcRow = bottomUp ? (height - 1 - y) : y;
      const float * px = rgba + (size_t)srcRow * width * 4;
      for (int x = 0; x < width; x++)
      {
        rowR[x] = px[x*4 + 0];
        rowG[x] = px[x*4 + 1];
        rowB[x] = px[x*4 + 2];
        rowA[x] = px[x*4 + 3];
      }
      uint32_t yc = (uint32_t)y, ds = rowBytes;
      f.write((const char*)&yc, 4);
      f.write((const char*)&ds, 4);
      f.write((const char*)rowA.data(), width * 4); // A
      f.write((const char*)rowB.data(), width * 4); // B
      f.write((const char*)rowG.data(), width * 4); // G
      f.write((const char*)rowR.data(), width * 4); // R
    }

    f.close();
    return f.good() || f.eof();
  }
}
