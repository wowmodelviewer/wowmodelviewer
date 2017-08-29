/*
 * HardDriveFile.h
 *
 *  Created on: 21 dec. 2015
 *      Author: Jerome
 */

#ifndef _HARDDRIVEFILE_H_
#define _HARDDRIVEFILE_H_

#include "CASCFile.h"

class QFile;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _HARDDRIVEFILE_API_ __declspec(dllexport)
#    else
#        define _HARDDRIVEFILE_API_ __declspec(dllimport)
#    endif
#else
#    define _HARDDRIVEFILE_API_
#endif

class _HARDDRIVEFILE_API_ HardDriveFile : public CASCFile
{
  public:
    HardDriveFile(QString path, QString realpath, int id = -1);
    ~HardDriveFile();

  protected:
    virtual bool openFile();
    virtual bool getFileSize(unsigned int & s);
    virtual unsigned long readFile();
    virtual bool doPostCloseOperation();


  private:
    bool opened;
    QString realpath;
    QFile * file;
};




#endif /* _HARDDRIVEFILE_H_ */
