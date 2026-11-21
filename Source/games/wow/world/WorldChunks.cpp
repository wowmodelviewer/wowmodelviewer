/*
 * WorldChunks.cpp  (port of Wow/World/WorldChunks.cs)
 */

#include "WorldChunks.h"

#include <cmath>
#include <cstring>

#include "WorldText.h"

namespace wmv
{
namespace world
{
using text::Hex;

WorldParseException::WorldParseException(const std::string& file, const std::string& chunk, long long offset,
                                         const std::string& reason)
  : WowParseException((file.empty() ? std::string("world file") : file) + ": " + (chunk.empty() ? std::string("?") : chunk) +
                      " at offset 0x" + Hex(offset) + ": " + reason),
    File(file), Chunk(chunk), Offset(offset), Reason(reason)
{
}

const char* ToString(WorldDataStatus s)
{
  switch (s)
  {
    case WorldDataStatus::Absent: return "Absent";
    case WorldDataStatus::Decoded: return "Decoded";
    case WorldDataStatus::PartiallyDecoded: return "PartiallyDecoded";
    case WorldDataStatus::Unsupported: return "Unsupported";
    case WorldDataStatus::Malformed: return "Malformed";
  }
  return "?";
}

const char* ToString(WorldIssueKind k)
{
  switch (k)
  {
    case WorldIssueKind::Malformed: return "Malformed";
    case WorldIssueKind::Unsupported: return "Unsupported";
    case WorldIssueKind::Unexpected: return "Unexpected";
  }
  return "?";
}

std::string WorldIssue::ToString() const
{
  return std::string(world::ToString(Kind)) + ": " + File + ": " + Chunk + " at offset 0x" + Hex(Offset) + ": " + Reason;
}

std::string WorldChunk::ToString() const
{
  return Tag + "@0x" + Hex(DataOffset) + "+" + std::to_string(Size);
}

std::string WorldUnsupportedChunk::ToString() const
{
  return Tag + "@0x" + Hex(DataOffset) + "+" + std::to_string(Size) + " (" + Reason + ")";
}

// ------------------------------------------------------------------------------------------- WorldChunkWalker

std::vector<WorldChunk> WorldChunkWalker::Walk(const std::vector<uint8_t>& data, int start, int end, const std::string& file,
                                               const std::string& context)
{
  const long long len = static_cast<long long>(data.size());
  if (start < 0 || end < start || end > len)
    throw WorldParseException(file, context, start,
                              "range [" + std::to_string(start) + ", " + std::to_string(end) + ") is outside the " +
                                std::to_string(len) + "-byte buffer");
  std::vector<WorldChunk> list;
  long long p = start;
  while (p < end)
  {
    if (end - p < 8)
      throw WorldParseException(file, context, p,
                                std::to_string(end - p) + " trailing byte(s) cannot hold a chunk header");
    int i = static_cast<int>(p);
    std::string tag = TagAt(data, i);
    uint32_t size = static_cast<uint32_t>(data[i + 4]) | (static_cast<uint32_t>(data[i + 5]) << 8) |
                    (static_cast<uint32_t>(data[i + 6]) << 16) | (static_cast<uint32_t>(data[i + 7]) << 24);
    long long payload = p + 8;
    if (static_cast<long long>(size) > end - payload)
      throw WorldParseException(file, Printable(tag), p,
                                "chunk claims " + std::to_string(size) + " byte(s), only " + std::to_string(end - payload) +
                                  " remain in the " + context);
    WorldChunk c;
    c.Tag = tag;
    c.HeaderOffset = i;
    c.DataOffset = static_cast<int>(payload);
    c.Size = static_cast<int>(size);
    list.push_back(c);
    p = payload + size;
  }
  return list;
}

std::string WorldChunkWalker::TagAt(const std::vector<uint8_t>& data, int offset)
{
  if (offset < 0 || static_cast<long long>(offset) > static_cast<long long>(data.size()) - 4)
    throw WorldParseException(std::string(), "chunk", offset, "a chunk tag at this offset is outside the buffer");
  std::string s(4, '\0');
  s[0] = static_cast<char>(data[offset + 3]);
  s[1] = static_cast<char>(data[offset + 2]);
  s[2] = static_cast<char>(data[offset + 1]);
  s[3] = static_cast<char>(data[offset]);
  return s;
}

std::string WorldChunkWalker::Printable(const std::string& tag)
{
  for (char ch : tag)
  {
    unsigned char u = static_cast<unsigned char>(ch);
    if (u < 0x20 || u > 0x7E)
      return "chunk";
  }
  return tag;
}

int WorldChunkWalker::Count(const std::vector<WorldChunk>& chunks, const std::string& tag)
{
  int n = 0;
  for (const WorldChunk& c : chunks)
    if (c.Tag == tag)
      n++;
  return n;
}

bool WorldChunkWalker::FindFirst(const std::vector<WorldChunk>& chunks, const std::string& tag, const std::string& file,
                                 std::vector<WorldIssue>& issues, WorldChunk& found)
{
  found = WorldChunk();
  bool have = false;
  for (const WorldChunk& c : chunks)
  {
    if (c.Tag != tag)
      continue;
    if (have)
    {
      issues.emplace_back(WorldIssueKind::Malformed, file, tag, c.HeaderOffset,
                          "appears more than once (never in the audited files); the first copy is used");
      continue;
    }
    found = c;
    have = true;
  }
  return have;
}

bool WorldChunkWalker::FindSingle(const std::vector<WorldChunk>& chunks, const std::string& tag, const std::string& file,
                                  WorldChunk& found)
{
  found = WorldChunk();
  bool have = false;
  for (const WorldChunk& c : chunks)
  {
    if (c.Tag != tag)
      continue;
    if (have)
      throw WorldParseException(file, tag, c.HeaderOffset, "appears more than once");
    found = c;
    have = true;
  }
  return have;
}

// ------------------------------------------------------------------------------------------- WorldSpan

WorldSpan::WorldSpan(const std::vector<uint8_t>& data, const WorldChunk& c, const std::string& f)
  : WorldSpan(data, c.DataOffset, c.Size, f, c.Tag)
{
}

WorldSpan::WorldSpan(const std::vector<uint8_t>& data, int st, int sz, const std::string& f, const std::string& t)
  : d(data.data()), start(st), size(sz), file(f), tag(t)
{
  const long long len = static_cast<long long>(data.size());
  if (st < 0 || sz < 0 || st > len || sz > len - st)
    throw WorldParseException(f, t, st, "chunk extends past the end of the buffer");
}

void WorldSpan::Require(int offset, int count) const
{
  if (offset < 0 || count < 0 || offset > size || count > size - offset)
    throw WorldParseException(file, tag, static_cast<long long>(start) + offset,
                              "read of " + std::to_string(count) + " byte(s) at +0x" + Hex(offset) + " is outside the " +
                                std::to_string(size) + "-byte chunk");
}

int WorldSpan::RecordCount(int recordSize, const std::string& name) const
{
  if (recordSize <= 0 || size % recordSize != 0)
    throw WorldParseException(file, tag, start,
                              std::to_string(size) + " byte(s) is not a whole number of " + std::to_string(recordSize) +
                                "-byte " + name + " records");
  return size / recordSize;
}

uint8_t WorldSpan::U8(int o) const
{
  Require(o, 1);
  return d[start + o];
}

uint16_t WorldSpan::U16(int o) const
{
  Require(o, 2);
  const uint8_t* p = d + start + o;
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t WorldSpan::U32(int o) const
{
  Require(o, 4);
  const uint8_t* p = d + start + o;
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t WorldSpan::U64(int o) const
{
  Require(o, 8);
  return static_cast<uint64_t>(U32(o)) | (static_cast<uint64_t>(U32(o + 4)) << 32);
}

float WorldSpan::F32Raw(int o) const
{
  uint32_t bits = U32(o);
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

float WorldSpan::F32(int o) const
{
  float f = F32Raw(o);
  if (!std::isfinite(f))
    throw WorldParseException(file, tag, static_cast<long long>(start) + o, "non-finite float");
  return f;
}

std::vector<uint8_t> WorldSpan::Bytes(int o, int count) const
{
  Require(o, count);
  return std::vector<uint8_t>(d + start + o, d + start + o + count);
}

std::vector<uint32_t> WorldSpan::U32Array() const
{
  int n = RecordCount(4, "u32");
  std::vector<uint32_t> r(static_cast<size_t>(n));
  for (int i = 0; i < n; i++)
    r[static_cast<size_t>(i)] = U32(4 * i);
  return r;
}
} // namespace world
} // namespace wmv
