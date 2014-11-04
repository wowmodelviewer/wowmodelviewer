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

class GameFile
{
  public:
    GameFile():eof(true),buffer(0),pointer(0),size(0) {}
    virtual ~GameFile() {}

    size_t read(void* dest, size_t bytes);
    size_t getSize();
    size_t getPos();
    unsigned char* getBuffer();
    unsigned char* getPointer();
    bool isEof();
    void seek(ssize_t offset);
    void seekRelative(ssize_t offset);
    void close();
    virtual void openFile(std::string filename) = 0;

  protected:
    bool eof;
    unsigned char *buffer;
    size_t pointer, size;

  private:
    // disable copying
    GameFile(const GameFile &);
    void operator=(const GameFile &);
};



#endif /* _GAMEFILE_H_ */
