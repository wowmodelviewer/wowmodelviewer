#include "CASCFile.h"

#ifndef __CASCLIB_SELF__
#define __CASCLIB_SELF__
#endif
#include "CascLib.h"
#include "CASCChunks.h"
#include "ChunkReader.h"
#include "Game.h"
#include "Logger.h"
// #define DEBUG_READ

CASCFile::CASCFile(std::string path, int id) : GameFile(std::move(path), id), m_handle(nullptr)
{
}

CASCFile::~CASCFile()
{
	close();
}

size_t CASCFile::read(void* dest, size_t bytes)
{
	if (m_useMemoryBuffer)
	{
		return GameFile::read(dest, bytes);
	}
	else
	{
		unsigned long result = 0;
		if (!CascReadFile(m_handle, dest, static_cast<DWORD>(bytes), &result))
			LOG_ERROR << "Reading" << filepath << "failed." << "Error" << GetLastError();

		return result;
	}
}

void CASCFile::seek(size_t offset)
{
	if (m_useMemoryBuffer)
	{
		GameFile::seek(offset);
	}
	else
	{
		if (CascSetFilePointer(m_handle, static_cast<LONG>(offset), nullptr, FILE_BEGIN) == CASC_INVALID_POS)
			LOG_ERROR << "Seek in file" << filepath << "to position" << offset << "failed. Error" << GetLastError();
	}
}

bool CASCFile::openFile()
{
	if ((m_fileDataId > 0 && GAMEDIRECTORY.openFile(m_fileDataId, &m_handle))
		|| GAMEDIRECTORY.openFile(filepath, &m_handle))
	{
		return true;
	}
	LOG_ERROR << "Opening" << filepath << "(ID:" << m_fileDataId << ")" << "failed." << "Error" << GetLastError();
	return false;
}

bool CASCFile::isAlreadyOpened()
{
	if (m_handle)
		return true;
	else
		return false;
}

bool CASCFile::getFileSize(unsigned long long& s)
{
	bool result = false;

	if (m_handle)
	{
		s = CascGetFileSize(m_handle, nullptr);

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
	if (!ChunkReader::isChunked(buffer, size))
		return;

	const auto parsed = ChunkReader::parse(buffer, size);
	for (const auto& ci : parsed)
	{
		Chunk chunk;
		chunk.magic   = ci.magic;
		chunk.start   = ci.start;
		chunk.size    = ci.size;
		chunk.pointer = 0;
		chunks.push_back(std::move(chunk));
	}

	if (chunks.size() == 1)
		setChunk(chunks[0].magic);
}

bool CASCFile::doPostCloseOperation()
{
#ifdef DEBUG_READ
  LOG_INFO << this << __FUNCTION__ << "Closing" << filepath << "handle" << m_handle;
#endif
	if (m_handle)
	{
		HANDLE savedHandle = m_handle;
		m_handle = nullptr;

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
			setChunk("AFID");
			while (!isEof())
			{
				AFID afid;
				read(&afid, sizeof(AFID));
				const GameFile* f = GAMEDIRECTORY.getFile(afid.fileId);
				if (f)
					LOG_INFO << f->fullname();
			}
		}
		else if (it.magic == "SKS1")
		{
			setChunk("SKS1");
			SKS1 sks1;
			read(&sks1, sizeof(sks1));
			LOG_INFO << "Chunk :" << it.magic.c_str() << "nGlobalSequences" << sks1.nGlobalSequences << "nAnimations" <<
				sks1.nAnimations << "nAnimationLookup" << sks1.nAnimationLookup;
		}
		else if (it.magic == "SKA1")
		{
			setChunk("SKA1");
			SKA1 ska1;
			read(&ska1, sizeof(ska1));
			LOG_INFO << "Chunk :" << it.magic.c_str() << "nAttachments" << ska1.nAttachments << "nAttachLookup" << ska1.
				nAttachLookup;
		}
		else if (it.magic == "SKB1")
		{
			setChunk("SKB1");
			SKB1 skb1;
			read(&skb1, sizeof(skb1));
			LOG_INFO << "Chunk :" << it.magic.c_str() << "nBones" << skb1.nBones << "nKeyBoneLookup" << skb1.
				nKeyBoneLookup;
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
				const GameFile* f = GAMEDIRECTORY.getFile(id);
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
