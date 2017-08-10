/*
 * HardDriveFile.cpp
 *
 *  Created on: 21 dec. 2015
 *      Author: Jerome
 */

#include "HardDriveFile.h"

#include <QFile>

#include "logger/Logger.h"


HardDriveFile::HardDriveFile(QString path, QString real, int id)
 : GameFile(path, id), opened(false), realpath(real)
{
}

HardDriveFile::~HardDriveFile()
{
  close();
}


bool HardDriveFile::open()
{
#ifdef DEBUG_READ
  LOG_INFO << __FUNCTION__ << "Opening" << filepath;
#endif

  if(opened)
  {
#ifdef DEBUG_READ
    LOG_INFO << filepath << "is already opened";
#endif
    return true;
  }

  eof = true;
  QFile file(realpath);
  if(!file.open(QIODevice::ReadOnly))
  {
    LOG_ERROR << "Opening" << filepath << "failed.";
    return false;
  }

  size = file.size();
  buffer = new unsigned char[size];
  file.read((char *)buffer, size);

  if(size == 0)
    eof = true;
  else
    eof = false;

  // @TODO : remove code duplicate from CASCFile
  // if MD21 file, deal with chunks
  if ((size >= 4) && (buffer[0] == 'M' && buffer[1] == 'D' && buffer[2] == '2' && buffer[3] == '1'))
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

  opened = true;
  file.close();

  return true;
}

bool HardDriveFile::close()
{
#ifdef DEBUG_READ
  LOG_INFO << __FUNCTION__ << "Closing" << filepath;
#endif
  if(opened)
  {
    GameFile::close();
    opened = false;
  }

  return true;
}
