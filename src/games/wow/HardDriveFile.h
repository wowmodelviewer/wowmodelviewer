/*
 * HardDriveFile.h
 *
 *  Created on: 21 dec. 2015
 *      Author: Jerome
 */

#ifndef _HARDDRIVEFILE_H_
#define _HARDDRIVEFILE_H_

#include "GameFile.h"


#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _HARDDRIVEFILE_API_ __declspec(dllexport)
#    else
#        define _HARDDRIVEFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _HARDDRIVEFILE_API_
#endif

class _HARDDRIVEFILE_API_ HardDriveFile : public GameFile
{
  public:
    HardDriveFile(QString path, QString realpath);
    ~HardDriveFile();
    bool open();
    bool close();

  private:
    bool opened;
    QString realpath;
};




#endif /* _HARDDRIVEFILE_H_ */
