#include "GameFile.h"
#include <cstring> // memcpy
#include "logger\Logger.h"

size_t GameFile::read(void* dest, size_t bytes)
{
	if (eof)
		return 0;

	const size_t rpos = pointer + bytes;
	if (rpos > size)
	{
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

void GameFile::seek(size_t offset)
{
	pointer = offset;
	eof = (pointer >= size);
}

void GameFile::seekRelative(size_t offset)
{
	pointer += offset;
	eof = (pointer >= size);
}

bool GameFile::open(bool useMemoryBuffer /* = true */)
{
	if (isAlreadyOpened())
		return true;

	eof = true;

	LOG_INFO << "GameFile::open - openFile() for " << filepath;
	if (!openFile())
		return false;

	m_useMemoryBuffer = useMemoryBuffer;

	if (getFileSize(size))
	{
		LOG_INFO << "GameFile::open - size=" << size << " buffered=" << m_useMemoryBuffer << " for " << filepath;
		if (m_useMemoryBuffer)
		{
			allocate(size);

			LOG_INFO << "GameFile::open - readFile() for " << filepath;
			if (readFile() != 0)
				eof = false;

			LOG_INFO << "GameFile::open - doPostOpenOperation() for " << filepath;
			doPostOpenOperation();
		}
	}

	LOG_INFO << "GameFile::open - done for " << filepath;
	return true;
}

bool GameFile::close()
{
	delete[] originalBuffer;
	originalBuffer = nullptr;
	buffer = nullptr;
	eof = true;
	chunks.clear();
	return doPostCloseOperation();
}

void GameFile::allocate(unsigned long long s)
{
	if (originalBuffer)
		delete[] originalBuffer;

	size = s;

	originalBuffer = new unsigned char[size];
	buffer = originalBuffer;

	if (size == 0)
		eof = true;
	else
		eof = false;
}

bool GameFile::setChunk(std::string chunkName, bool resetToStart)
{
	bool result = false;

	// save current pointer if a chunk is currently under reading
	for (auto& it : chunks)
	{
		if (it.magic == curChunk)
		{
			it.pointer = pointer;
			break;
		}
	}

	for (auto& it : chunks)
	{
		if (it.magic == chunkName)
		{
			buffer = originalBuffer + it.start;
			pointer = (resetToStart ? 0 : it.pointer);
			size = it.size;
			curChunk = chunkName;
			result = true;
			eof = (pointer >= size);
			break;
		}
	}

	// if (!result)
	//   LOG_ERROR << __FUNCTION__ << "Cannot find chunk" << chunkName.c_str();

	return result;
}

size_t GameFile::getSize()
{
	return size;
}

size_t GameFile::getPos()
{
	return pointer;
}

unsigned char* GameFile::getBuffer() const
{
	return buffer;
}

unsigned char* GameFile::getPointer()
{
	return buffer + pointer;
}

void GameFile::dumpStructure()
{
	LOG_INFO << "Structure for file" << filepath;
	for (auto it : chunks)
		LOG_INFO << "Chunk :" << it.magic.c_str() << it.start << it.size;
}
