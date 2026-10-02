#ifndef ANIMATION_FILE_SNAPSHOT_H
#define ANIMATION_FILE_SNAPSHOT_H
#include "GameFile.h"
#include <cstdint>
#include <cstring>
#include <vector>

// Borrow an already-open file without changing its chunk/cursor or closing it.
// Files opened here are closed on every exit, including allocation failures.
inline bool readAnimationSnapshot(GameFile &file,
                                  std::vector<unsigned char> &bytes) {
  const bool borrowed = file.isCurrentlyOpen();
  if (!borrowed && !file.open())
    return false;
  struct CloseOwned {
    GameFile &file;
    bool owned;
    ~CloseOwned() {
      if (owned)
        file.close();
    }
  } close{file, !borrowed};
  const auto *raw = file.rawBuffer();
  const size_t size = file.rawSize();
  if (!raw || !size)
    return false;
  size_t start = 0, count = size;
  if (file.isChunked()) {
    // AFSB offsets are relative to its payload. Other chunks must not shift
    // them.
    bool found = false;
    size_t chunkCount = 0, singleStart = 0, singleCount = 0;
    for (size_t at = 0; at <= size && size - at >= 8;) {
      uint32_t length;
      std::memcpy(&length, raw + at + 4, sizeof(length));
      if (length > size - at - 8)
        return false;
      ++chunkCount;
      singleStart = at + 8;
      singleCount = length;
      if (!std::memcmp(raw + at, "AFSB", 4)) {
        start = at + 8;
        count = length;
        found = true;
        break;
      }
      at += 8 + length;
    }
    // Preserve legacy single-chunk and whole-file layouts when there is no
    // AFSB.
    if (!found && chunkCount == 1) {
      start = singleStart;
      count = singleCount;
    }
  }
  bytes.assign(raw + start, raw + start + count);
  return true;
}
#endif
