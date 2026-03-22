#pragma once

#include <string>
#include <vector>
#include "Component.h"

#define _GAMEFILE_API_

/// @brief Abstract base class representing a file within the game data archive.
///
/// Provides a memory-buffered read interface with seek, chunk navigation,
/// and subclass hooks for the actual I/O backend (CASC, hard-drive, etc.).
class _GAMEFILE_API_ GameFile : public Component
{
public:
	GameFile(std::string path, int id = -1)
		: eof(true), buffer(nullptr), pointer(0), size(0),
		  filepath(std::move(path)), m_useMemoryBuffer(true), m_fileDataId(id),
		  originalBuffer(nullptr), curChunk("")
	{
	}

	virtual ~GameFile() = default;

	/// @brief Read bytes from the file into dest.
	/// @return Number of bytes actually read.
	virtual size_t read(void* dest, size_t bytes);

	/// @brief Total size of the file in bytes.
	size_t getSize();

	/// @brief Current read position.
	size_t getPos();

	/// @brief Pointer to the start of the internal buffer.
	unsigned char* getBuffer() const;

	/// @brief Pointer to the current read position within the buffer.
	unsigned char* getPointer();

	/// @brief True if the read pointer has reached the end of the file.
	bool isEof();

	/// @brief Seek to an absolute byte offset.
	virtual void seek(size_t offset);

	/// @brief Advance the read pointer by offset bytes.
	void seekRelative(size_t offset);

	/// @brief Open the file, optionally loading into a memory buffer.
	/// @return true on success.
	bool open(bool useMemoryBuffer = true);

	/// @brief Close the file and release the internal buffer.
	bool close();

	void setFullName(const std::string& name) { filepath = name; }
	const std::string& fullname() const { return filepath; }
	int fileDataId() { return m_fileDataId; }

	/// @brief Allocate (or reallocate) the internal buffer to the given size.
	void allocate(unsigned long long size);

	/// @brief Switch the active read window to the named chunk.
	/// @param chunkName     Four-character chunk identifier.
	/// @param resetToStart  If true, reset the read pointer to the chunk start.
	/// @return true if the chunk was found.
	bool setChunk(std::string chunkName, bool resetToStart = true);

	/// @brief True if the file has been parsed into named chunks.
	bool isChunked() { return chunks.size() > 0; }

	virtual void dumpStructure();

protected:
	virtual bool openFile() = 0;
	virtual bool isAlreadyOpened() = 0;
	virtual bool getFileSize(unsigned long long& s) = 0;
	virtual unsigned long readFile() = 0;
	virtual void doPostOpenOperation() = 0;
	virtual bool doPostCloseOperation() = 0;

	bool eof;
	unsigned char* buffer;
	unsigned long long pointer, size;
	std::string filepath;
	int m_fileDataId;

	struct chunkHeader
	{
		char magic[4];
		unsigned __int32 size;
	};

	struct Chunk
	{
		std::string magic;
		unsigned int start;
		unsigned int size;
		unsigned int pointer;
	};

	std::vector<Chunk> chunks;
	bool m_useMemoryBuffer;

private:
	// disable copying
	GameFile(const GameFile&);
	void operator=(const GameFile&);
	unsigned char* originalBuffer;
	std::string curChunk;
};
