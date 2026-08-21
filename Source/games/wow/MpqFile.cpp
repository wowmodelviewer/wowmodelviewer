/*
 * MpqFile.cpp
 */

#include "MpqFile.h"

#include <algorithm> // std::find
#include <cstring>   // memcpy

#ifndef __STORMLIB_NO_STATIC_LINK__
#  define __STORMLIB_NO_STATIC_LINK__
#endif
#include "StormLib.h"

#include "Game.h" // GAMEDIRECTORY
#include "logger/Logger.h"

// Chunk-magic table lives in CASCFile.cpp; chunk detection is storage-agnostic, so reuse it
// rather than duplicate the list. (Old MPQ M2s are not chunked, so this stays a no-op for them.)
extern std::vector<std::string> KNOWN_CHUNKS;

wow::MpqFile::MpqFile(QString path)
  : GameFile(path, -1), m_handle(0)
{
}

wow::MpqFile::~MpqFile()
{
  close();
}

bool wow::MpqFile::openFile()
{
  // Legacy clients have no FileDataID -> open strictly by name, routed through the active
  // provider (WoWFolder::openFile -> MpqFileProvider::openByName).
  if (GAMEDIRECTORY.openFile(filepath.toStdString(), &m_handle))
    return true;

  LOG_ERROR << "[mpq] Opening" << filepath << "failed.";
  return false;
}

bool wow::MpqFile::isAlreadyOpened()
{
  return m_handle != 0;
}

bool wow::MpqFile::getFileSize(unsigned long long & s)
{
  if (!m_handle)
    return false;

  DWORD high = 0;
  DWORD low = SFileGetFileSize(m_handle, &high);
  if (low == SFILE_INVALID_SIZE)
  {
    LOG_ERROR << "[mpq] Size query for" << filepath << "failed.";
    // GameFile::open() returns success as long as openFile() succeeded, so close the handle
    // here on a size failure (corrupt entry) rather than holding it open until destruction.
    SFileCloseFile(m_handle);
    m_handle = 0;
    return false;
  }

  s = (static_cast<unsigned long long>(high) << 32) | low;
  return true;
}

unsigned long wow::MpqFile::readFile()
{
  DWORD result = 0;
  // size is 64-bit but SFileReadFile takes a DWORD; every real WoW MPQ file is far below 4 GB
  // (same assumption CASCFile makes with CascReadFile), so the cast is safe in practice.
  // SFileReadFile returns FALSE at end-of-file even when it filled the buffer, so only treat a
  // zero-byte read as a real failure.
  if (!SFileReadFile(m_handle, buffer, static_cast<DWORD>(size), &result, nullptr) && result == 0)
    LOG_ERROR << "[mpq] Reading" << filepath << "failed.";

  return result;
}

void wow::MpqFile::doPostOpenOperation()
{
  if (size >= sizeof(chunkHeader))
  {
    chunkHeader chunkHead;
    memcpy(&chunkHead, buffer, sizeof(chunkHeader));
    std::string magic = std::string(chunkHead.magic, 4);
    if (std::find(KNOWN_CHUNKS.begin(), KNOWN_CHUNKS.end(), magic) != KNOWN_CHUNKS.end()
        && chunkHead.size + sizeof(chunkHeader) <= size) // header + payload must fit the file
    {
      unsigned int offset = 0;
      while (offset + sizeof(chunkHeader) <= size) // a trailing partial header is corruption, not a chunk
      {
        chunkHeader ChunkHead;
        memcpy(&ChunkHead, buffer + offset, sizeof(chunkHeader));
        offset += sizeof(chunkHeader);

        Chunk chunk;
        chunk.magic = std::string(ChunkHead.magic, 4);
        chunk.start = offset;
        chunk.size = ChunkHead.size;
        chunk.pointer = 0;
        chunks.push_back(chunk);

        offset += ChunkHead.size;
      }

      if (chunks.size() == 1)
        setChunk(chunks[0].magic);
    }
  }
}

bool wow::MpqFile::doPostCloseOperation()
{
  if (m_handle)
  {
    void * saved = m_handle;
    m_handle = 0;
    return SFileCloseFile(saved) != FALSE;
  }
  return true;
}
