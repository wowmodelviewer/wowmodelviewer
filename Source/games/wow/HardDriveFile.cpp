#include "HardDriveFile.h"

#include "logger/Logger.h"

HardDriveFile::HardDriveFile(QString path, QString real, int id)
	: CASCFile(path, id), opened(false), realpath(std::move(real)), file(nullptr)
{
}

HardDriveFile::~HardDriveFile()
{
	close();
}

bool HardDriveFile::openFile()
{
	file = new std::ifstream(realpath.toStdWString(), std::ios::binary);

	if (!file->is_open())
	{
		LOG_ERROR << "Opening" << filepath << "failed.";
		delete file;
		file = nullptr;
		return false;
	}

	opened = true;
	return true;
}

bool HardDriveFile::isAlreadyOpened()
{
	if (opened)
		return true;
	else
		return false;
}

bool HardDriveFile::getFileSize(unsigned long long& s)
{
	if (!file || !file->is_open())
		return false;

	const auto cur = file->tellg();
	file->seekg(0, std::ios::end);
	s = static_cast<unsigned long long>(file->tellg());
	file->seekg(cur);
	return true;
}

unsigned long HardDriveFile::readFile()
{
	if (!file || !file->is_open())
		return 0;

	file->seekg(0, std::ios::beg);
	file->read(reinterpret_cast<char*>(buffer), size);
	const unsigned long s = static_cast<unsigned long>(file->gcount());
	file->close();
	delete file;
	file = nullptr;
	return s;
}

bool HardDriveFile::doPostCloseOperation()
{
#ifdef DEBUG_READ
  LOG_INFO << __FUNCTION__ << "Closing" << filepath;
#endif
	if (opened)
		opened = false;

	return true;
}
