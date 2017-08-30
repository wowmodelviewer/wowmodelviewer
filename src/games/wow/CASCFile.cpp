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

bool  CASCFile::openFile()
{
  if (m_handle) // already opened
  {
#ifdef DEBUG_READ
    LOG_INFO << filepath << "is already opened";
#endif
    return true;
  }

  m_handle = GAMEDIRECTORY.openFile(filepath.toStdString());

  if (!m_handle)
  {
    LOG_ERROR << "Opening" << filepath << "failed." << "Error" << GetLastError();
    return false;
  }

  return true;
}

bool CASCFile::getFileSize(unsigned int & s)
{
  bool result = false;
  
  if (m_handle)
  {
    s = CascGetFileSize(m_handle, 0);
  
    if (s == CASC_INVALID_SIZE)
      LOG_ERROR << "Opening" << filepath << "failed." << "Error" << GetLastError();
    else
      result = true;
  }

  return result;
}

unsigned long CASCFile::readFile()
{
  unsigned long result = 0;
  
  if (!CascReadFile(m_handle, buffer, size, &result))
    LOG_ERROR << "Reading" << filepath << "failed." << "Error" << GetLastError();
  
  return result;
}

void CASCFile::doPostOpenOperation()
{
  // md21 file, need to read all chunks
  if ((size >= 4) && (buffer[0] == 'M' && buffer[1] == 'D' && buffer[2] == '2' && buffer[3] == '1'))
  {
    unsigned int offset = 0;

    LOG_INFO << "Parsing chunks for file" << filepath;
    while (offset < size)
    {
      chunkHeader chunkHead;
      memcpy(&chunkHead, buffer + offset, sizeof(chunkHeader));
      offset += sizeof(chunkHeader);

      Chunk * chunk = new Chunk();
      chunk->magic = std::string(chunkHead.magic, 4);
      chunk->start = offset;
      chunk->size = chunkHead.size;
      chunk->pointer = 0;
      chunks.push_back(*chunk);

      LOG_INFO << "Chunk :" << chunk->magic.c_str() << chunk->size;

      offset += chunkHead.size;
    }
  }
}

bool CASCFile::doPostCloseOperation()
{
#ifdef DEBUG_READ
  LOG_INFO << this << __FUNCTION__ << "Closing" << filepath << "handle" << m_handle;
#endif
  if(m_handle)
  {
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
