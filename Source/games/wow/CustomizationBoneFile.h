#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace customization
{
  // Source values only. Application is specified in CustomizationBoneTransform.h;
  // parsing never bakes a selected face into the shared bind pose or animation.
  struct BoneCorrection
  {
    uint16_t bone;
    std::array<float, 16> matrix;
  };

  inline bool readBoneFile(const unsigned char * data, size_t size, size_t boneCount,
                           std::vector<BoneCorrection> & result, std::string & error)
  {
    result.clear();
    error.clear();
    const auto fail = [&](const char * message) { error = message; return false; };
    const auto u32 = [](const unsigned char * p) {
      return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    };
    if (!data || size < 4 || u32(data) != 1) return fail("Unsupported bone-file version.");
    const unsigned char * ids = nullptr;
    const unsigned char * matrices = nullptr;
    size_t idSize = 0, matrixSize = 0;
    for (size_t offset = 4; offset < size;)
    {
      if (size - offset < 8) return fail("Truncated bone-file chunk header.");
      const auto * header = data + offset;
      const size_t bytes = u32(header + 4);
      offset += 8;
      if (bytes > size - offset) return fail("Truncated bone-file chunk.");
      if (std::memcmp(header, "BIDA", 4) == 0)
      {
        if (ids) return fail("Duplicate bone-index chunk.");
        ids = data + offset;
        idSize = bytes;
      }
      else if (std::memcmp(header, "BOMT", 4) == 0)
      {
        if (matrices) return fail("Duplicate bone-matrix chunk.");
        matrices = data + offset;
        matrixSize = bytes;
      }
      else return fail("Unknown bone-file chunk.");
      offset += bytes;
    }
    if (!ids || !matrices || idSize % 2 || matrixSize % 64 || idSize / 2 != matrixSize / 64)
      return fail("Bone indices and matrices do not match.");
    std::set<uint16_t> seen;
    std::vector<BoneCorrection> parsed;
    for (size_t i = 0; i < idSize / 2; ++i)
    {
      BoneCorrection entry;
      entry.bone = uint16_t(ids[i*2]) | (uint16_t(ids[i*2+1]) << 8);
      if (entry.bone >= boneCount || !seen.insert(entry.bone).second)
        return fail("Invalid or duplicate customization bone index.");
      for (size_t j = 0; j < 16; ++j)
      {
        const uint32_t bits = u32(matrices + i*64 + j*4);
        static_assert(sizeof(float) == 4, "Bone files require 32-bit floats");
        std::memcpy(&entry.matrix[j], &bits, 4);
        if (!std::isfinite(entry.matrix[j])) return fail("Non-finite customization matrix.");
      }
      // Reject projective matrices. All observed version-1 files are affine.
      if (entry.matrix[3] != 0 || entry.matrix[7] != 0 || entry.matrix[11] != 0 || entry.matrix[15] != 1)
        return fail("Non-affine customization matrix.");
      parsed.push_back(entry);
    }
    result.swap(parsed);
    return true;
  }
}
