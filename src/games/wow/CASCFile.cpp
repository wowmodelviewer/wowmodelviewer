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
      unsigned char * bufferPtr = buffer;

      unsigned char * md21chunkStart = 0;
      unsigned __int32 md21chunkSize = 0;

      LOG_INFO << "Parsing chunks for file" << filepath;
      while (bufferPtr < buffer + nbBytesRead)
      {
        chunkHeader chunkHead;
        memcpy(&chunkHead, bufferPtr, sizeof(chunkHeader));
        bufferPtr += sizeof(chunkHeader);
        LOG_INFO << "Chunk :" << chunkHead.magic[0] << chunkHead.magic[1] << chunkHead.magic[2] << chunkHead.magic[3] << chunkHead.size;

        if (chunkHead.magic[0] == 'M' && chunkHead.magic[1] == 'D' && chunkHead.magic[2] == '2' && chunkHead.magic[3] == '1')
        {
          md21chunkStart = bufferPtr;
          md21chunkSize = chunkHead.size;
        }

        bufferPtr += chunkHead.size;
      }

      // relocate buffer on MD21 chunk
      if (md21chunkSize != 0)
      {
        // relocate buffer on chunk's data
        unsigned char *newBuffer = new unsigned char[md21chunkSize];
        memcpy(newBuffer, md21chunkStart, md21chunkSize);
        delete[] buffer;
        buffer = newBuffer;
        size = md21chunkSize; // real size is only chunk' size
      }
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
