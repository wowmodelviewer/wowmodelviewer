/*
 * CASCFile.cpp
 *
 *  Created on: 23 oct. 2014
 *      Author: Jerome
 */

#include "CASCFile.h"

#include "Game.h"
#include "globalvars.h"
#include "logger/Logger.h"
// #define DEBUG_READ


CASCFile::CASCFile(QString path, int id)
  : GameFile(path, id), m_handle(0)
{
}

CASCFile::~CASCFile()
{
  close();
}

struct chunkHeader
{
  char magic[4];
  unsigned __int32 size;
};


bool CASCFile::open()
{
#ifdef DEBUG_READ
  LOG_INFO << this << __FUNCTION__ << "Opening" << filepath << "handle" << m_handle;
#endif

  if(m_handle) // already opened
  {
#ifdef DEBUG_READ
  LOG_INFO << filepath << "is already opened";
#endif
    return true;
  }

  eof = true;
  m_handle = GAMEDIRECTORY.openFile(filepath.toStdString());

  if(!m_handle)
  {
    LOG_ERROR << "Opening" << filepath << "failed." << "Error" << GetLastError();
    return false;
  }

  if(m_handle)
  {
    DWORD nbBytesRead = 0;
    size = CascGetFileSize(m_handle,0);
    if(size == CASC_INVALID_SIZE)
    {
      LOG_ERROR << "Opening" << filepath << "failed." << "Error" << GetLastError();
      return false;
    }

    if(buffer)
      delete [] buffer;

    buffer = new unsigned char[size];
    if (!CascReadFile(m_handle, buffer, size, &nbBytesRead))
    {
      LOG_ERROR << "Reading" << filepath << "failed." << "Error" << GetLastError();
      return false;
    }

    if (nbBytesRead != 0)
      eof = false;

    // md21 file, need to read all chunks
    if ((nbBytesRead >= 4) && (buffer[0] == 'M' && buffer[1] == 'D' && buffer[2] == '2' && buffer[3] == '1'))
    {
      unsigned int offset = 0;

      LOG_INFO << "Parsing chunks for file" << filepath;
      while (offset < nbBytesRead)
      {
        chunkHeader chunkHead;
        memcpy(&chunkHead, buffer + offset, sizeof(chunkHeader));
        offset += sizeof(chunkHeader);
        LOG_INFO << "Chunk :" << chunkHead.magic[0] << chunkHead.magic[1] << chunkHead.magic[2] << chunkHead.magic[3] << chunkHead.size;

        Chunk * chunk = new Chunk();
        chunk->magic = std::string(chunkHead.magic,4);
        chunk->start = offset;
        chunk->size = chunkHead.size;
        chunks.push_back(*chunk);

        offset += chunkHead.size;
      }

      setChunk("MD21");
    }
#ifdef DEBUG_READ
    LOG_INFO << __FUNCTION__ <<  "|" << filepath << "nb bytes read =>" << nbBytesRead << "/" << size;
#endif
  }

  return true;
}

bool CASCFile::close()
{
#ifdef DEBUG_READ
  LOG_INFO << this << __FUNCTION__ << "Closing" << filepath << "handle" << m_handle;
#endif
  if(m_handle)
  {
    GameFile::close();
    HANDLE savedHandle = m_handle;
    m_handle = 0;

#ifdef DEBUG_READ
    bool result = CascCloseFile(savedHandle);
    LOG_INFO << __FUNCTION__ << result;
    return result;
#else
    return CascCloseFile(savedHandle);
#endif
  }

  return true;
}
