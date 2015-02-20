/*
 * CASCFile.h
 *
 *  Created on: 23 oct. 2014
 *      Author: Jerome
 */

#ifndef _CASCFILE_H_
#define _CASCFILE_H_

#include "casclib/src/CascLib.h"

#include "GameFile.h"

#include <string>

class CASCFolder;

class CASCFile : public GameFile
{
  public:
    CASCFile();
    CASCFile(const std::string & path);
    bool open();
    bool close();
    void openFile(std::string filename);

  private:
    HANDLE m_handle;
    std::string m_filePath;
};




#endif /* _CASCFILE_H_ */
