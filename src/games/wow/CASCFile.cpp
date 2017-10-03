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

std::vector<std::string> KNOWN_CHUNKS =
{
  "PFID",
  "SFID",
  "AFID",
  "BFID",
  "MD21",
  "TXAC",
  "EXPT",
  "EXP2",
  "PABC",
  "PADC",
  "PSBC",
  "PEDC",
  "SKID",
  "AFM2",
  "AFSA",
  "AFSB",
  /*
  "MOHD",
  "MOTX",
  "MOMT",
  "MOUV",
  "MOGN",
  "MOGI",
  "MOSB",
  "MOPV",
  "MOPT",
  "MOPR",
  "MOVV",
  "MOVB",
  "MOLT",
  "MODS",
  "MODN",
  "MODD",
  "MFOG",
  "MCVP",
  "GFID",
  "MOGP",
  "MOPY",
  "MOVI",
  "MOVT",
  "MONR",
  "MOTV",
  "MOBA",
  "MOLR",
  "MODR",
  "MOBN",
  "MOBR",
  "MOCV",
  "MLIQ",
  "MORI",
  "MORB",
  "MOTA",
  "MOBS",
  "MDAL",
  "MOPL",
  "MOPB",
  "MOLS",
  "MOLP"
  */
};

CASCFile::CASCFile(QString path, int id)
  : GameFile(path, id), m_handle(0)
{
}

CASCFile::~CASCFile()
{
  close();
}

bool  CASCFile::openFile()
{
  if (!GAMEDIRECTORY.openFile(filepath.toStdString(), &m_handle))
  {
    LOG_ERROR << "Opening" << filepath << "failed." << "Error" << GetLastError();
    return false;
  }

  return true;
}

bool CASCFile::isAlreadyOpened()
{
  if (m_handle)
    return true;
  else
    return false;
}

bool CASCFile::getFileSize(unsigned int & s)
{
  bool result = false;
  
  if (m_handle)
  {
    s = CascGetFileSize(m_handle, 0);
  
    if (s == CASC_INVALID_SIZE)
      LOG_ERROR << "Opening" << filepath << "failed." << "Error" << GetLastError();
    else
      result = true;
  }

  return result;
}

unsigned long CASCFile::readFile()
{
  unsigned long result = 0;
  
  if (!CascReadFile(m_handle, buffer, size, &result))
    LOG_ERROR << "Reading" << filepath << "failed." << "Error" << GetLastError();
  
  return result;
}

void CASCFile::doPostOpenOperation()
{
  if (size >= sizeof(chunkHeader))
  {
    chunkHeader chunkHead;
    memcpy(&chunkHead, buffer, sizeof(chunkHeader));
    std::string magic = std::string(chunkHead.magic, 4);
    if (std::find(KNOWN_CHUNKS.begin(), KNOWN_CHUNKS.end(), magic) != KNOWN_CHUNKS.end() 
        && chunkHead.size <= size)
    {
      unsigned int offset = 0;

      //LOG_INFO << "Parsing chunks for file" << filepath << "First chunk read :" << magic.c_str();
      while (offset < size)
      {
        chunkHeader chunkHead;
        memcpy(&chunkHead, buffer + offset, sizeof(chunkHeader));
        offset += sizeof(chunkHeader);

        Chunk * chunk = new Chunk();
        chunk->magic = std::string(chunkHead.magic, 4);
        chunk->start = offset;
        chunk->size = chunkHead.size;
        chunk->pointer = 0;
        chunks.push_back(*chunk);

        //LOG_INFO << "Chunk :" << chunk->magic.c_str() << chunk->start << chunk->size;

        offset += chunkHead.size;
      }

      // if there is only one chunk, set it
      if (chunks.size() == 1)
        setChunk(chunks[0].magic);
    }
  }
}

bool CASCFile::doPostCloseOperation()
{
#ifdef DEBUG_READ
  LOG_INFO << this << __FUNCTION__ << "Closing" << filepath << "handle" << m_handle;
#endif
  if(m_handle)
  {
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
