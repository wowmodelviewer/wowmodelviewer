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


CASCFile::CASCFile(QString path)
  : GameFile(path), m_handle(0), m_isMD21(false)
{
}

CASCFile::~CASCFile()
{
  close();
}


bool CASCFile::open()
{
#ifdef DEBUG_READ
  LOG_INFO << __FUNCTION__ << "Opening" << filepath;
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
    CascReadFile(m_handle, buffer, size, &nbBytesRead);
    if(nbBytesRead != 0)
      eof = false;
    else
      eof = true;
#ifdef DEBUG_READ
    LOG_INFO << __FUNCTION__ <<  "|" << filepath << "nb bytes read =>" << nbBytesRead << "/" << size;
#endif

    // if MD21 file, deal with chunks
    if ((nbBytesRead >= 4) && (buffer[0] == 'M' && buffer[1] == 'D' && buffer[2] == '2' && buffer[3] == '1'))
    {
      // read size of first chunk in the file
      unsigned __int32 chunkSize;
      memcpy(&chunkSize, buffer + 4, 4);

      // check if there is only one chunk in the file (if chunk size is at least 98% of the file)
      if ((float)chunkSize / (float)size >= 0.98)
      {
        LOG_INFO << __FUNCTION__ << __FILE__ << __LINE__ << "MD21 file detected";
        // relocate buffer on chunk's data
        unsigned char *newBuffer = new unsigned char[chunkSize];
        std::copy(buffer + 8, buffer + 8 + chunkSize, newBuffer);
        delete[] buffer;
        buffer = newBuffer;
        size = chunkSize; // real size is only chunk' size
      }
      // @todo else -> multiple chunks in the file, to be implemented when needed
    }
  }

  return true;
}

bool CASCFile::close()
{
#ifdef DEBUG_READ
  LOG_INFO << __FUNCTION__ << "Closing" << filepath << m_handle;
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
