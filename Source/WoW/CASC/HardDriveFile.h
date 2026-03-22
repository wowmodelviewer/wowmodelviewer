/*
 * HardDriveFile.h
 *
 *  Created on: 21 dec. 2015
 *      Author: Jerome
 */

#pragma once

#include "CASCFile.h"

#include <fstream>

/// @brief A CASCFile implementation that reads data from the local hard drive
///        rather than from a CASC archive.
class HardDriveFile : public CASCFile
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
