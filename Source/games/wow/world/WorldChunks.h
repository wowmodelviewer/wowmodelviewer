/*
 * WorldChunks.h  (port of Tools/UnityRendererProject/Assets/Scripts/Wow/World/WorldChunks.cs)
 *
 * Shared plumbing for the world-file parsers (WDT, split ADT): the chunk walk, a reader bounded to one chunk,
 * the exception a malformed required structure raises, and the issue list that keeps a malformed or
 * unsupported OPTIONAL structure from failing the whole file.
 *
 * Specification: docs/world-viewer/00-audit.md (the audit). Every WDT/ADT/WDL in the four audited clients is a
 * flat list of chunks -- a 4-byte tag stored byte-reversed ("REVM" for MVER), a little-endian u32 payload size,
 * then the payload -- and walks without a single error (audit 4.2, 5.1). Root MCNKs hold sub-chunks after a
 * 128-byte header; tex0/obj0 MCNKs from their first byte (audit 5.1).
 *
 * Ownership: nothing here keeps a reference to the caller's bytes past the call that reads them (WorldSpan is
 * a transient view used during a parse).
 *
 * C++ notes: a C# null string is an empty std::string here. Tags are std::string of 4 raw bytes.
 */

#ifndef _WMV_WORLD_WORLDCHUNKS_H_
#define _WMV_WORLD_WORLDCHUNKS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ByteCursor.h"
#include "WorldApi.h"

namespace wmv
{
namespace world
{
/// A required world structure that is malformed: the file cannot be used as the structure it claims to be.
/// Carries the file context, the chunk tag, the absolute byte offset and the reason.
class _WMVWORLD_API_ WorldParseException : public WowParseException
{
public:
  WorldParseException(const std::string& file, const std::string& chunk, long long offset, const std::string& reason);

  std::string File;
  std::string Chunk;
  long long Offset;
  std::string Reason;
};

/// How a parsed structure came out.
enum class WorldDataStatus
{
  /// Not in the file (or no bytes were supplied for it).
  Absent = 0,
  /// Decoded with the layout the audit proved.
  Decoded,
  /// Its boundaries are proven and checked, some fields are kept raw (meaning not established); nothing is
  /// invented.
  PartiallyDecoded,
  /// Present, but its layout or meaning is not established: listed (tag, offset, size), not decoded.
  Unsupported,
  /// Present but inconsistent with the proven layout; see the issue list.
  Malformed,
};

enum class WorldIssueKind
{
  /// An optional structure is inconsistent with its proven layout; it was not used, the rest of the file was.
  Malformed,
  /// A structure whose layout or meaning is not established (kept undecoded).
  Unsupported,
  /// Allowed by the layout but never seen in the audited clients (e.g. a rule that held in 100% of the corpus
  /// does not hold here): the data is kept, the claim is flagged.
  Unexpected,
};

_WMVWORLD_API_ const char* ToString(WorldDataStatus s);
_WMVWORLD_API_ const char* ToString(WorldIssueKind k);

/// A non-fatal finding about one structure, with enough context to cite the bytes.
struct _WMVWORLD_API_ WorldIssue
{
  WorldIssueKind Kind = WorldIssueKind::Malformed;
  std::string File;
  std::string Chunk;
  long long Offset = 0;
  std::string Reason;

  WorldIssue() = default;
  WorldIssue(WorldIssueKind kind, const std::string& file, const std::string& chunk, long long offset,
             const std::string& reason)
    : Kind(kind), File(file), Chunk(chunk), Offset(offset), Reason(reason)
  {
  }

  std::string ToString() const;
};

/// One chunk as found: its tag in reading order ("MVER"), where its 8-byte header starts, where its payload
/// starts, and the payload size -- all absolute offsets into the buffer it was walked in. Every chunk is
/// listed, decoded or not.
struct _WMVWORLD_API_ WorldChunk
{
  std::string Tag;
  int HeaderOffset = 0;
  int DataOffset = 0;
  int Size = 0;

  std::string ToString() const;
};

/// A chunk that is present but not decoded (layout or meaning not established).
struct _WMVWORLD_API_ WorldUnsupportedChunk
{
  std::string Tag;
  int DataOffset = 0;
  int Size = 0;
  std::string Reason;

  std::string ToString() const;
};

class _WMVWORLD_API_ WorldChunkWalker
{
public:
  /// Walk [start, end) of `data` as chunks. Throws WorldParseException for a header that does not fit, a size
  /// that runs past `end`, or a range outside the buffer -- never reads past `end`. `context` names the
  /// container in messages ("file", "MCNK 17").
  static std::vector<WorldChunk> Walk(const std::vector<uint8_t>& data, int start, int end, const std::string& file,
                                      const std::string& context);

  /// The tag at `offset`, stored byte-reversed: bytes "REVM" read as "MVER".
  static std::string TagAt(const std::vector<uint8_t>& data, int offset);

  /// The tag, or "chunk" when it holds non-printable bytes (keeps messages readable).
  static std::string Printable(const std::string& tag);

  static int Count(const std::vector<WorldChunk>& chunks, const std::string& tag);

  /// The first chunk with `tag`, or false, for OPTIONAL chunks: a repeat is reported as a Malformed issue and
  /// ignored (none of the audited files repeats these tags), so it cannot fail the rest of the file.
  static bool FindFirst(const std::vector<WorldChunk>& chunks, const std::string& tag, const std::string& file,
                        std::vector<WorldIssue>& issues, WorldChunk& found);

  /// The only chunk with `tag`, or false, for REQUIRED chunks. A second one is a WorldParseException: none of
  /// the audited structures repeats these tags, so a repeat is not guessed at.
  static bool FindSingle(const std::vector<WorldChunk>& chunks, const std::string& tag, const std::string& file,
                         WorldChunk& found);
};

/// Little-endian reads bounded to one chunk's payload. Offsets are relative to the payload; every read is
/// checked against the chunk (not just the buffer), and a failure names the file, the chunk, and the absolute
/// offset. A view: the buffer must outlive it.
class _WMVWORLD_API_ WorldSpan
{
public:
  WorldSpan(const std::vector<uint8_t>& data, const WorldChunk& c, const std::string& file);
  WorldSpan(const std::vector<uint8_t>& data, int start, int size, const std::string& file, const std::string& tag);

  int Size() const { return size; }
  int Start() const { return start; }
  const std::string& Tag() const { return tag; }

  void Require(int offset, int count) const;

  /// Number of whole `recordSize`-byte records; throws when the size is not a multiple (an inconsistent array
  /// size).
  int RecordCount(int recordSize, const std::string& name) const;

  uint8_t U8(int o) const;
  uint16_t U16(int o) const;
  uint32_t U32(int o) const;
  int32_t I32(int o) const { return static_cast<int32_t>(U32(o)); }
  uint64_t U64(int o) const;
  /// The float32 as stored (non-finite values are returned, not rejected).
  float F32Raw(int o) const;
  /// A float32 that must be finite (positions, heights, extents).
  float F32(int o) const;
  std::vector<uint8_t> Bytes(int o, int count) const;
  std::vector<uint32_t> U32Array() const;
  long long AbsoluteOffset(int o) const { return static_cast<long long>(start) + o; }

private:
  const uint8_t* d;
  int start;
  int size;
  std::string file;
  std::string tag;
};
} // namespace world
} // namespace wmv

#endif
