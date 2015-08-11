/*
 * GameFile.cpp
 *
 *  Created on: 27 oct. 2014
 *      Author: Jerome
 */


#include "GameFile.h"

#include <cstring> // memcpy

size_t GameFile::read(void* dest, size_t bytes)
{
  if (eof)
    return 0;

  size_t rpos = pointer + bytes;
  if (rpos > size) {
    bytes = size - pointer;
    eof = true;
  }

  memcpy(dest, &(buffer[pointer]), bytes);

  pointer = rpos;

  return bytes;
}

bool GameFile::isEof()
{
    return eof;
}

void GameFile::seek(size_t offset) {
  pointer = offset;
  eof = (pointer >= size);
}

void GameFile::seekRelative(size_t offset)
{
  pointer += offset;
  eof = (pointer >= size);
}

void GameFile::close()
{
  delete [] buffer;
  eof = true;
}

size_t GameFile::getSize()
{
  return size;
}

size_t GameFile::getPos()
{
  return pointer;
}

unsigned char* GameFile::getBuffer()
{
  return buffer;
}

unsigned char* GameFile::getPointer()
{
  return buffer + pointer;
}
