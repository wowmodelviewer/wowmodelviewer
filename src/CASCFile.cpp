/*
 * CASCFile.cpp
 *
 *  Created on: 23 oct. 2014
 *      Author: Jerome
 */

#include "CASCFile.h"
#include "CASCFolder.h"
#include "globalvars.h"
#include "modelviewer.h"
#include <iostream>

//#define DEBUG_READ

CASCFile::CASCFile()
: GameFile(),m_handle(NULL), m_folder(NULL)
{
#ifdef DEBUG_READ
  std::cout << __FUNCTION__ << " 2" << std::endl;
#endif
}

CASCFile::CASCFile(const std::string & path, CASCFolder * parent)
 : GameFile(), m_filePath(path), m_folder(parent)
{
#ifdef DEBUG_READ
  std::cout << __FUNCTION__ << " 1" << std::endl;
#endif
  openFile(path);
}


bool CASCFile::open()
{
  bool result = false;
  if(m_folder)
    result = CascOpenFile(m_folder->hStorage, m_filePath.c_str(), m_folder->CASCLocale(), 0, &m_handle);
  else
    m_handle = NULL;
#ifdef DEBUG_READ
  std::cout << __FUNCTION__ << " result => " << std::boolalpha << result << std::endl;
#endif
  return result;
}

bool CASCFile::close()
{
  return CascCloseFile(m_handle);
}

void CASCFile::openFile(std::string filename)
{
#ifdef DEBUG_READ
  std::cout << __FUNCTION__ << " opening => " << filename << std::endl;
#endif
  m_filePath = filename;
  m_folder = g_modelViewer->gameFolder;
  if(open())
   {
     DWORD nbBytesRead = 0;
     size = CascGetFileSize(m_handle,0);
     buffer = new unsigned char[size];
     CascReadFile(m_handle, buffer, size, &nbBytesRead);
     if(nbBytesRead != 0)
       eof = false;
#ifdef DEBUG_READ
     std::cout << __FUNCTION__ <<  " | " << m_filePath << " nb bytes read => " << nbBytesRead << " / " << size << std::endl;
#endif
   }
}
