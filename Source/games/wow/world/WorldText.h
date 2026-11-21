/*
 * WorldText.h
 *
 * Internal (not exported) string helpers for the world parsers' messages: the C# originals use
 * string.Format with {0:X} (upper-case hex) and {0:R} (round-trip float). Header-only, used by the .cpp files.
 */

#ifndef _WMV_WORLD_TEXT_H_
#define _WMV_WORLD_TEXT_H_

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace wmv
{
namespace world
{
namespace text
{
// {0:X}: upper-case hexadecimal, no prefix (a negative value prints as its 64-bit two's complement).
inline std::string Hex(long long v)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(v));
  return buf;
}

inline std::string Num(long long v) { return std::to_string(v); }

// {0:R} for a float32: the shortest decimal that reads back as the same float.
inline std::string FloatR(float f)
{
  if (std::isnan(f))
    return "NaN";
  if (std::isinf(f))
    return f < 0 ? "-Infinity" : "Infinity";
  char buf[64];
  for (int precision = 6; precision <= 9; precision++)
  {
    std::snprintf(buf, sizeof(buf), "%.*g", precision, static_cast<double>(f));
    if (std::strtof(buf, nullptr) == f)
      break;
  }
  return buf;
}
} // namespace text
} // namespace world
} // namespace wmv

#endif
