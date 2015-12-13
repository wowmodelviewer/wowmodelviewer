/*
 * CASCFile.cpp
 *
 *  Created on: 23 oct. 2014
 *      Author: Jerome
 */

#include "CASCFile.h"

#include <iostream>

#include "GameDirectory.h"
#include "globalvars.h"
#include "logger/Logger.h"
// #define DEBUG_READ

CASCFile::CASCFile()
: GameFile(), m_handle(NULL),  m_filePath("")
{
#ifdef DEBUG_READ
  std::cout << __FUNCTION__ << " 2" << std::endl;
#endif
}

CASCFile::CASCFile(const std::string & path)
 : GameFile(), m_filePath(path)
{
#ifdef DEBUG_READ
  std::cout << __FUNCTION__ << " 1" << std::endl;
#endif
  openFile(path);
}


bool CASCFile::open()
{
  m_handle = GAMEDIRECTORY.openFile(m_filePath);

  if(!m_handle)
    LOG_ERROR << "Opening" << m_filePath.c_str() << "failed." << "Error" << GetLastError();

  return m_handle;
}

bool CASCFile::close()
{
  if(m_handle)
    return CascCloseFile(m_handle);
  else
    return true;
}

void CASCFile::openFile(std::string filename)
{
#ifdef DEBUG_READ
  std::cout << __FUNCTION__ << " opening => " << filename << std::endl;
#endif
  m_filePath = filename;
  if(open())
  {
    DWORD nbBytesRead = 0;
    size = CascGetFileSize(m_handle,0);
    if(buffer)
      delete [] buffer;
    buffer = new unsigned char[size];
    CascReadFile(m_handle, buffer, size, &nbBytesRead);
    if(nbBytesRead != 0)
      eof = false;
    else
      eof = true;
#ifdef DEBUG_READ
    std::cout << __FUNCTION__ <<  " | " << m_filePath << " nb bytes read => " << nbBytesRead << " / " << size << std::endl;
#endif
  }
  else
  {
    eof = true;
  }
}
