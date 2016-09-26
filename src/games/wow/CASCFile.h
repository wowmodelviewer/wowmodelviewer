/*
 * CASCFile.h
 *
 *  Created on: 23 oct. 2014
 *      Author: Jerome
 */

#ifndef _CASCFILE_H_
#define _CASCFILE_H_

#include "CascLib.h"

#include <string>
#include "GameFile.h"

class CASCFolder;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _CASCFILE_API_ __declspec(dllexport)
#    else
#        define _CASCFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _CASCFILE_API_
#endif

class _CASCFILE_API_ CASCFile : public GameFile
{
  public:
    CASCFile(QString path);
    ~CASCFile();
    bool open();
    bool close();
    unsigned char* getBuffer();

  private:
    HANDLE m_handle;
    bool m_isMD21;
};




#endif /* _CASCFILE_H_ */
