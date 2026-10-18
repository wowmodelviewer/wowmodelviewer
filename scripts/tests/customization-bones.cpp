#include "CustomizationBoneFile.h"
#include "CustomizationBoneTransform.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void require(bool value) { if (!value) throw std::runtime_error("bone reader check failed"); }
static void u32(std::vector<unsigned char> & bytes, uint32_t v)
{ for (int i = 0; i < 4; ++i) bytes.push_back((v >> (8*i)) & 255); }

int main(int argc, char ** argv)
{
  std::vector<unsigned char> data;
  u32(data, 1);
  data.insert(data.end(), {'B','I','D','A'}); u32(data, 2);
  data.push_back(3); data.push_back(0);
  data.insert(data.end(), {'B','O','M','T'}); u32(data, 64);
  for (int i = 0; i < 16; ++i) u32(data, i % 5 == 0 ? 0x3f800000 : 0);
  std::vector<customization::BoneCorrection> result;
  std::string error;
  auto read = [&](const std::vector<unsigned char> & bytes, size_t count = 4) {
    return customization::readBoneFile(bytes.data(), bytes.size(), count, result, error);
  };
  require(read(data) && result.size() == 1 && result[0].bone == 3);
  for (size_t n = 0; n < data.size(); ++n)
    require(!read(std::vector<unsigned char>(data.begin(), data.begin()+n)) && result.empty());
  require(!read(data, 3) && result.empty());
  auto bad = data; bad[0] = 2; require(!read(bad));
  bad = data; bad[4] = 'X'; require(!read(bad));
  bad = data; bad[8] = 255; require(!read(bad));
  bad = data; bad.insert(bad.end(), data.begin()+4, data.begin()+14); require(!read(bad));
  bad = data; bad[22+2] = 0x80; bad[22+3] = 0x7f; require(!read(bad)); // infinity
  bad = data; bad[22+15*4] = 1; require(!read(bad)); // non-affine
  size_t realFiles = 0, realBones = 0;
  size_t referenceSamples = 0;
  float maxReferenceError = 0;
  for (int i = 1; i < argc; ++i)
  {
    if (std::string(argv[i]) == "--reference")
    {
      require(++i < argc);
      std::ifstream input(argv[i], std::ios::binary);
      uint32_t count = 0;
      require(bool(input.read(reinterpret_cast<char *>(&count), sizeof(count))) && count > 0 && count < 1000000);
      for (uint32_t row = 0; row < count; ++row)
      {
        customization::Matrix animated, correction, expected;
        std::array<float, 3> pivot;
        require(bool(input.read(reinterpret_cast<char *>(animated.data()), sizeof(animated))));
        require(bool(input.read(reinterpret_cast<char *>(pivot.data()), sizeof(pivot))));
        require(bool(input.read(reinterpret_cast<char *>(correction.data()), sizeof(correction))));
        require(bool(input.read(reinterpret_cast<char *>(expected.data()), sizeof(expected))));
        const auto actual = customization::applyToSkinLocal(animated, correction, pivot);
        for (size_t j = 0; j < 16; ++j)
        {
          const float difference = std::abs(expected[j] - actual[j]);
          require(std::isfinite(difference) && difference < 2e-6f);
          if (difference > maxReferenceError) maxReferenceError = difference;
        }
        ++referenceSamples;
      }
      require(input.peek() == std::ifstream::traits_type::eof());
      continue;
    }
    std::ifstream input(argv[i], std::ios::binary);
    require(input.good());
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    require(read(bytes, 239));
    ++realFiles; realBones += result.size();
  }
  // The joint-local form must leave the animated origin alone except for the
  // correction's translation, even when correction and animation do not commute.
  auto animated = customization::translation(3, 4, 5);
  animated[0] = 0; animated[1] = 1; animated[4] = -1; animated[5] = 0;
  auto correction = customization::translation(.2f, -.3f, .4f);
  correction[5] = 0; correction[6] = 1; correction[9] = -1; correction[10] = 0;
  const auto applied = customization::applyToJointLocal(animated, correction);
  require(std::abs(applied[12]-3.2f) < 1e-6f && std::abs(applied[13]-3.7f) < 1e-6f && std::abs(applied[14]-5.4f) < 1e-6f);
  const auto unchanged = customization::applyToJointLocal(animated, customization::translation(0,0,0));
  require(unchanged == animated);
  std::cout << "Malformed-file checks passed; real files=" << realFiles << ", corrections=" << realBones
    << "; reference samples=" << referenceSamples << ", max error=" << maxReferenceError << '\n';
}
