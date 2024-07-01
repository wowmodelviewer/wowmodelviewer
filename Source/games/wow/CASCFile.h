/*
 * CASCFile.h
 *
 *  Created on: 23 oct. 2014
 *      Author: Jerome
 */

#pragma once

#include "GameFile.h"

typedef void* HANDLE;

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
	CASCFile(QString path, int id = -1);
	~CASCFile();

	// re implemented from GameFile
	size_t read(void* dest, size_t bytes);
	void seek(size_t offset);
	void dumpStructure();

protected:
	virtual bool openFile();
	virtual bool isAlreadyOpened();
	virtual bool getFileSize(unsigned long long& s);
	virtual unsigned long readFile();
	virtual void doPostOpenOperation();
	virtual bool doPostCloseOperation();

private:
	HANDLE m_handle;
};
