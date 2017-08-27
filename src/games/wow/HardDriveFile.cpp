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

  allocate(file.size());
  file.read((char *)buffer, size);

  // @TODO : rework to avoid duplicate code from CASCFile
  // md21 file, need to read all chunks
  if ((size >= 4) && (buffer[0] == 'M' && buffer[1] == 'D' && buffer[2] == '2' && buffer[3] == '1'))
  {
    unsigned int offset = 0;

    LOG_INFO << "Parsing chunks for file" << realpath;
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
