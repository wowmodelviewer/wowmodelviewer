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
  : CASCFile(path, id), opened(false), realpath(real), file(0)
{
}

HardDriveFile::~HardDriveFile()
{
  close();
}

bool HardDriveFile::openFile()
{
  if (opened)
  {
#ifdef DEBUG_READ
    LOG_INFO << filepath << "is already opened";
#endif
    return true;
  }

  file = new QFile(realpath);
  
  if (!file->open(QIODevice::ReadOnly))
  {
    LOG_ERROR << "Opening" << filepath << "failed.";
    return false;
  }

  opened = true;
  return true;
}

bool HardDriveFile::getFileSize(unsigned int & s)
{
  if (!file && !file->isOpen())
    return false;

  s = file->size();
  return true;
}

unsigned long HardDriveFile::readFile()
{
  if (!file && !file->isOpen())
    return 0;

  unsigned long s = file->read((char *)buffer, size);
  file->close();
  delete file;
  file = 0;
  return s;
}

bool HardDriveFile::doPostCloseOperation()
{
#ifdef DEBUG_READ
  LOG_INFO << __FUNCTION__ << "Closing" << filepath;
#endif
  if(opened)
    opened = false;

  return true;
}
