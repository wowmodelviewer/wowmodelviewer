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

#include "metaclasses/Component.h"

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
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
    GameFile():eof(true),buffer(0),pointer(0),size(0) {}
    virtual ~GameFile() {}

    size_t read(void* dest, size_t bytes);
    size_t getSize();
    size_t getPos();
    unsigned char* getBuffer();
    unsigned char* getPointer();
    bool isEof();
    void seek(size_t offset);
    void seekRelative(size_t offset);
    void close();
    virtual void openFile(std::string filename) = 0;

    void setFullName(QString & name) { m_fullName = name; }
    QString fullname() { return m_fullName; }

  protected:
    bool eof;
    unsigned char *buffer;
    size_t pointer, size;

  private:
    // disable copying
    GameFile(const GameFile &);
    void operator=(const GameFile &);

    QString m_fullName;
};



#endif /* _GAMEFILE_H_ */
