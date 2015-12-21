/*
 * HardDriveFile.cpp
 *
 *  Created on: 21 dec. 2015
 *      Author: Jerome
 */

#include "HardDriveFile.h"

#include <QFile>

#include "logger/Logger.h"


HardDriveFile::HardDriveFile(QString path, QString real)
 : GameFile(path), opened(false), realpath(real)
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
