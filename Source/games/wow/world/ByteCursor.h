/*
 * ByteCursor.h  (port of Tools/UnityRendererProject/Assets/Scripts/Wow/ByteCursor.cs)
 *
 * Bounds-checked little-endian reader for raw WoW asset bytes. Every read validates against the buffer
 * length, so a truncated or malformed asset raises WowParseException with a useful message instead of
 * reading out of bounds somewhere deep in a parser -- or, worse, silently reading neighbouring memory.
 *
 * Plain C++: no Qt, no graphics API.
 */

#ifndef _WMV_WORLD_BYTECURSOR_H_
#define _WMV_WORLD_BYTECURSOR_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "WorldApi.h"

namespace wmv
{
namespace world
{
/// A malformed, truncated or unsupported WoW asset. Always carries context.
class _WMVWORLD_API_ WowParseException : public std::runtime_error
{
public:
  explicit WowParseException(const std::string& message);
};

/// (count, offset) pair as stored in M2/skin headers.
struct M2Array
{
  int Count = 0;
  int Offset = 0;

  M2Array() = default;
  M2Array(int count, int offset) : Count(count), Offset(offset) {}
  std::string ToString() const;
};

/// Reads from a caller-owned buffer; the buffer must outlive the cursor.
class _WMVWORLD_API_ ByteCursor
{
public:
  ByteCursor(const std::vector<uint8_t>& data, const std::string& what, int start = 0);

  int Position() const { return pos; }
  void SetPosition(int value);
  int Length() const { return static_cast<int>(data->size()); }
  const std::vector<uint8_t>& Data() const { return *data; }

  /// Throws unless [offset, offset+count) lies inside the buffer.
  void Require(int offset, int count) const;
  void Seek(int offset);

  uint8_t ReadByte();
  int8_t ReadSByte() { return static_cast<int8_t>(ReadByte()); }
  uint16_t ReadUInt16();
  int16_t ReadInt16() { return static_cast<int16_t>(ReadUInt16()); }
  uint32_t ReadUInt32();
  int32_t ReadInt32() { return static_cast<int32_t>(ReadUInt32()); }
  /// A float32 that must be finite.
  float ReadSingle();
  /// Four bytes in file order as characters.
  std::string ReadMagic();

  /// An M2Array: a (count, offset) pair. The offset is relative to the start of the structure the array
  /// belongs to (for an M2 that is the MD21 payload, not the file), which is why the caller resolves it.
  M2Array ReadArray();

  /// Validate that an array of fixed-size elements fits inside the buffer.
  void RequireArray(const M2Array& arr, int elementSize, const std::string& name) const;

private:
  const std::vector<uint8_t>* data;
  std::string what; // asset description, for error messages
  int pos;
};
} // namespace world
} // namespace wmv

#endif
