/*
 * CASCFile.cpp
 *
 *  Created on: 23 oct. 2014
 *      Author: Jerome
 */

#include "CASCFile.h"

#include "CASCChunks.h"
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
  "SKL1",
  "SKA1",
  "SKB1",
  "SKS1",
  "SKPD"
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

void CASCFile::dumpStructure()
{
  LOG_INFO << "Structure for file" << filepath;
  for (auto it : chunks)
  {
    if (it.magic == "AFID")
    {
      LOG_INFO << "Chunk :" << it.magic.c_str() << "nb anim file id" << it.size / sizeof(AFID);
    }
    else if (it.magic == "SKS1")
    {
      setChunk("SKS1");
      SKS1 sks1;
      read(&sks1, sizeof(sks1));
      LOG_INFO << "Chunk :" << it.magic.c_str() << "nGlobalSequences" << sks1.nGlobalSequences << "nAnimations" << sks1.nAnimations << "nAnimationLookup" << sks1.nAnimationLookup;
    }
    else if (it.magic == "SKA1")
    {
      setChunk("SKA1");
      SKA1 ska1;
      read(&ska1, sizeof(ska1));
      LOG_INFO << "Chunk :" << it.magic.c_str() << "nAttachments" << ska1.nAttachments << "nAttachLookup" << ska1.nAttachLookup;
    }
    else if (it.magic == "SKB1")
    {
      setChunk("SKB1");
      SKB1 skb1;
      read(&skb1, sizeof(skb1));
      LOG_INFO << "Chunk :" << it.magic.c_str() << "nBones" << skb1.nBones << "nKeyBoneLookup" << skb1.nKeyBoneLookup;
    }
    else if (it.magic == "SKPD")
    {
      setChunk("SKPD");
      SKPD skpd;
      read(&skpd, sizeof(skpd));
      LOG_INFO << "Chunk :" << it.magic.c_str() << "parentFileId" << skpd.parentFileId;
    }
    else if (it.magic == "BFID")
    {
      LOG_INFO << "Chunk :" << it.magic.c_str() << "nb bone files id" << it.size / sizeof(uint32);
      setChunk("BFID");
      while (!isEof())
      {
        uint32 id;
        read(&id, sizeof(uint32));
        GameFile * f = GAMEDIRECTORY.getFile(id);
        if (f)
          LOG_INFO << f->fullname();
      }
    }
    else
    {
      LOG_INFO << "Chunk :" << it.magic.c_str() << it.start << it.size;
    }
  }
}
