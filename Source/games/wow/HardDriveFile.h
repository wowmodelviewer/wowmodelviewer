/*
 * HardDriveFile.h
 *
 *  Created on: 21 dec. 2015
 *      Author: Jerome
 */

#pragma once

#include "CASCFile.h"

#include <fstream>

#define _HARDDRIVEFILE_API_

class _HARDDRIVEFILE_API_ HardDriveFile : public CASCFile
{
public:
	HardDriveFile(std::string path, std::string realpath, int id = -1);
	~HardDriveFile();

protected:
	virtual bool openFile();
	virtual bool isAlreadyOpened();
	virtual bool getFileSize(unsigned long long& s);
	virtual unsigned long readFile();
	virtual bool doPostCloseOperation();

private:
	bool opened;
	std::string realpath;
	std::ifstream* file;
};
