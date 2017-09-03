/*
 * GameFile.h
 *
 *  Created on: 27 oct. 2014
 *      Author: Jerome
 */

#ifndef _GAMEFILE_H_
#define _GAMEFILE_H_

#include <stdio.h>
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
      : eof(true), buffer(0), pointer(0), size(0), 
        filepath(path), m_fileDataId(id), originalBuffer(0),
        curChunk("")
    {}

    virtual ~GameFile() {}

    size_t read(void* dest, size_t bytes);
    size_t getSize();
    size_t getPos();
    unsigned char* getBuffer();
    unsigned char* getPointer();
    bool isEof();
    void seek(size_t offset);
    void seekRelative(size_t offset);
    bool open();
    bool close();
    
    void setFullName(const QString & name) { filepath = name; }
    QString fullname() const { return filepath; }
    int fileDataId() { return m_fileDataId; }

    void allocate(unsigned int size);
    bool setChunk(std::string chunkName, bool resetToStart = true);
    bool isChunked() { return chunks.size() > 0; }

  protected:

    virtual bool openFile() = 0;
    virtual bool isAlreadyOpened() = 0;
    virtual bool getFileSize(unsigned int & s) = 0;
    virtual unsigned long readFile() = 0;
    virtual void doPostOpenOperation() = 0;
    virtual bool doPostCloseOperation() = 0;

    bool eof;
    unsigned char *buffer;
    unsigned int pointer, size;
    QString filepath;

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

  private:
    // disable copying
    GameFile(const GameFile &);
    void operator=(const GameFile &);

    int m_fileDataId;
    unsigned char * originalBuffer;
    std::string curChunk;
};



#endif /* _GAMEFILE_H_ */
