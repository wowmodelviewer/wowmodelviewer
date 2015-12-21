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
 : GameFile(path), m_handle(0)
{
}

CASCFile::~CASCFile()
{
  LOG_INFO << __FUNCTION__ << this;
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
