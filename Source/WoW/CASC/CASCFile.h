/*
 * CASCFile.h
 *
 *  Created on: 23 oct. 2014
 *      Author: Jerome
 */

#pragma once

#include "GameFile.h"

#ifdef _WIN32
#include <Windows.h>  // Include Windows headers for standard HANDLE definition
#endif

class CASCFolder;

/// @brief GameFile implementation that reads from a CASC storage archive.
class CASCFile : public GameFile
{
public:
	CASCFile(std::string path, int id = -1);
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
