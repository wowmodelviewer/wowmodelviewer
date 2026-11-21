/*
 * ByteCursor.cpp  (port of Wow/ByteCursor.cs)
 */

#include "ByteCursor.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace wmv
{
namespace world
{
WowParseException::WowParseException(const std::string& message) : std::runtime_error(message) {}

std::string M2Array::ToString() const
{
  return "count=" + std::to_string(Count) + " offset=" + std::to_string(Offset);
}

ByteCursor::ByteCursor(const std::vector<uint8_t>& d, const std::string& w, int start)
  : data(&d), what(w.empty() ? std::string("asset") : w), pos(start)
{
}

void ByteCursor::SetPosition(int value)
{
  Require(value, 0);
  pos = value;
}

void ByteCursor::Require(int offset, int count) const
{
  const long long len = static_cast<long long>(data->size());
  if (offset < 0 || count < 0 || offset > len || count > len - offset)
    throw WowParseException(what + ": read of " + std::to_string(count) + " byte(s) at offset " + std::to_string(offset) +
                            " is outside the " + std::to_string(len) + "-byte asset");
}

void ByteCursor::Seek(int offset)
{
  Require(offset, 0);
  pos = offset;
}

uint8_t ByteCursor::ReadByte()
{
  Require(pos, 1);
  return (*data)[pos++];
}

uint16_t ByteCursor::ReadUInt16()
{
  Require(pos, 2);
  const uint8_t* p = data->data() + pos;
  uint16_t v = static_cast<uint16_t>(p[0] | (p[1] << 8));
  pos += 2;
  return v;
}

uint32_t ByteCursor::ReadUInt32()
{
  Require(pos, 4);
  const uint8_t* p = data->data() + pos;
  uint32_t v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
  pos += 4;
  return v;
}

float ByteCursor::ReadSingle()
{
  uint32_t bits = ReadUInt32();
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  if (!std::isfinite(f))
    throw WowParseException(what + ": non-finite float at offset " + std::to_string(pos - 4));
  return f;
}

std::string ByteCursor::ReadMagic()
{
  Require(pos, 4);
  std::string s(reinterpret_cast<const char*>(data->data() + pos), 4);
  pos += 4;
  return s;
}

M2Array ByteCursor::ReadArray()
{
  uint32_t count = ReadUInt32();
  uint32_t offset = ReadUInt32();
  return M2Array(static_cast<int>(count), static_cast<int>(offset));
}

void ByteCursor::RequireArray(const M2Array& arr, int elementSize, const std::string& name) const
{
  if (arr.Count == 0)
    return;
  if (arr.Count < 0 || arr.Offset < 0)
    throw WowParseException(what + ": " + name + " has a negative count/offset (" + std::to_string(arr.Count) + "/" +
                            std::to_string(arr.Offset) + ")");
  long long bytes = static_cast<long long>(arr.Count) * elementSize;
  if (bytes > INT_MAX)
    throw WowParseException(what + ": " + name + " claims " + std::to_string(arr.Count) + " elements -- implausible");
  Require(arr.Offset, static_cast<int>(bytes));
}
} // namespace world
} // namespace wmv
