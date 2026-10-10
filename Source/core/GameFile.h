/*
 * GameFile.h
 *
 *  Created on: 27 oct. 2014
 *      Author: Jerome
 */

#ifndef _GAMEFILE_H_
#define _GAMEFILE_H_

#include <string>
#include <vector>

#include "metaclasses/Component.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _GAMEFILE_API_ __declspec(dllexport)
#    else
#        define _GAMEFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _GAMEFILE_API_
#endif

class _GAMEFILE_API_ GameFile : public Component
{
  public:
    GameFile(QString path, int id = -1) 
      : eof(true), buffer(nullptr), pointer(0), size(0), 
        filepath(path), m_useMemoryBuffer(true), m_fileDataId(id),
        originalBuffer(nullptr), originalSize(0), curChunk("")
    {}

    virtual ~GameFile() {}

    virtual size_t read(void* dest, size_t bytes);
    size_t getSize();
    size_t getPos();
    unsigned char* getBuffer() const;
    unsigned char* getPointer();
    bool isEof();
    virtual void seek(size_t offset);
    void seekRelative(size_t offset);
    bool open(bool useMemoryBuffer = true);
    bool close();
    // After open() in memory mode: whether the whole file was read into the buffer. open() still
    // succeeds after a short read, as its callers expect; one that must not pass partial data on
    // checks this.
    bool readComplete() const { return m_readComplete; }
    // True while the file is open (shared, folder-owned objects: another component may hold
    // it open; opening/closing it again from outside would corrupt that reader's state).
    bool isCurrentlyOpen() { return isAlreadyOpened(); }
    
    void setFullName(const QString & name) { filepath = name; }
    QString fullname() const { return filepath; }
    int fileDataId() { return m_fileDataId; }

    void allocate(unsigned long long size);
    bool setChunk(std::string chunkName, bool resetToStart = true);
    bool isChunked() { return chunks.size() > 0; }

    // Whole memory allocation, independent of the selected chunk and its declared size.
    const unsigned char * rawBuffer() const { return originalBuffer; }
    size_t rawSize() const { return originalSize; }

    virtual void dumpStructure();

  protected:

    virtual bool openFile() = 0;
    virtual bool isAlreadyOpened() = 0;
    virtual bool getFileSize(unsigned long long & s) = 0;
    virtual unsigned long readFile() = 0;
    virtual void doPostOpenOperation() = 0;
    virtual bool doPostCloseOperation() = 0;

    bool eof;
    unsigned char *buffer;
    unsigned long long pointer, size;
    QString filepath;
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
    GameFile(const GameFile &);
    void operator=(const GameFile &);
    unsigned char * originalBuffer;
    size_t originalSize;
    std::string curChunk;
    bool m_readComplete = false;
};



#endif /* _GAMEFILE_H_ */
