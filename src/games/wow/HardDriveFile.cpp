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
  : GameFile(path, id), opened(false), realpath(real), file(0)
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

void HardDriveFile::doPostOpenOperation()
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
      chunks.push_back(*chunk);

      LOG_INFO << "Chunk :" << chunk->magic.c_str() << chunk->size;

      offset += chunkHead.size;
    }
  }
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
