/*
 * CASCFolder.cpp
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#include "CASCFolder.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <locale>
#include <map>
#include <utility>

#include <QFile>
#include <QRegularExpression>

#include "CASCFile.h"
#include "logger/Logger.h"

CASCFolder * CASCFolder::m_instance = 0;

std::map<QString,QString> globalNameMap;
std::vector<std::pair<QString,QString>> folderFileList;

CASCFolder::CASCFolder()
 : hStorage(NULL),m_currentLocale(""), m_currentCascLocale(CASC_LOCALE_NONE), m_folder(""), m_openError(ERROR_SUCCESS)
{

}

void CASCFolder::init(const std::string &folder)
{
  m_folder = folder;

  if(m_folder.find_last_of("\\") == m_folder.length()-1)
    m_folder = m_folder.substr (0,m_folder.length()-1);

  initLocales();
  initVersion();
}

void CASCFolder::initLocales()
{
  LOG_INFO << "Determining Locale for:" << m_folder.c_str();

  std::string buildinfofile = m_folder+"\\..\\.build.info";
  std::ifstream buildinfo(buildinfofile.c_str());

  if(!buildinfo.good())
  {
    LOG_ERROR << "Fail to open .build.info to determine locale";
    return;
  }

  std::string line, prevline;
  // clean up any previously found locale
  m_localesFound.clear();
  while(!buildinfo.eof())
  {
    buildinfo >> line;
    if(!prevline.empty() && prevline.find("text?") == std::string::npos)
    {
      if(line.find("text?") != std::string::npos)
        m_localesFound.push_back(prevline);
    }
    prevline = line;
  }

  if(m_localesFound.empty())
  {
    LOG_ERROR << "Fail to grab locale in .build.info file";
  }
  else
  {
    for(size_t i = 0; i < m_localesFound.size(); i++)
    {
      LOG_INFO << "locale found : " << m_localesFound[i].c_str();
    }
  }
}


bool CASCFolder::setLocale(std::string locale)
{
  // init map based on CASCLib
  std::map<std::string,int> locales;
  locales["frFR"] = CASC_LOCALE_FRFR;
  locales["deDE"] = CASC_LOCALE_DEDE;
  locales["esES"] = CASC_LOCALE_ESES;
  locales["esMX"] = CASC_LOCALE_ESMX;
  locales["ptBR"] = CASC_LOCALE_PTBR;
  locales["itIT"] = CASC_LOCALE_ITIT;
  locales["ptPT"] = CASC_LOCALE_PTPT;
  locales["enGB"] = CASC_LOCALE_ENGB;
  locales["ruRU"] = CASC_LOCALE_RURU;
  locales["enUS"] = CASC_LOCALE_ENUS;
  locales["enCN"] = CASC_LOCALE_ENCN;
  locales["enTW"] = CASC_LOCALE_ENTW;
  locales["koKR"] = CASC_LOCALE_KOKR;
  locales["zhCN"] = CASC_LOCALE_ZHCN;
  locales["zhTW"] = CASC_LOCALE_ZHTW;

  if(!locale.empty())
  {
    std::map<std::string,int>::iterator it = locales.find(locale);

    if(it != locales.end())
    {
      HANDLE dummy;
      LOG_INFO << "Loading Game Folder:" << m_folder.c_str();
      // locale found => try to open it
      if(!CascOpenStorage(m_folder.c_str(), it->second, &hStorage))
      {
        m_openError = GetLastError();
        LOG_ERROR << "Opening" << m_folder.c_str() << "failed." << "Error" << m_openError;
        return false;
      }

      if(CascOpenFile(hStorage,"Interface\\FrameXML\\Localization.lua", it->second, 0, &dummy))
      {
        CascCloseFile(dummy);
        m_currentCascLocale = it->second;
        m_currentLocale = locale;
        LOG_INFO << "Locale succesfully set:" << m_currentLocale.c_str();
      }
      else
      {
        LOG_ERROR << "Setting Locale" << locale.c_str() << "for folder" << m_folder.c_str() << "failed";
        return false;
      }
    }
  }
  return true;
}


void CASCFolder::initVersion()
{
  QString buildinfofile = QString::fromStdString(m_folder)+"\\..\\.build.info";
  LOG_INFO << "buildinfofile : " << buildinfofile;

  QFile file(buildinfofile);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open .build.info to determine game version";
    return;
  }

  QTextStream in(&file);
  QString line;

  // read first line and grab VERSION index
  line = in.readLine();

  QStringList headers = line.split('|');
  int index = 0;
  for( ; index < headers.size() ; index++)
  {
    if(headers[index].contains("VERSION", Qt::CaseInsensitive))
      break;
  }

  // now that we have index, let's get actual values
  line = in.readLine();
  QStringList values = line.split('|');
  QRegularExpression re("^(\\d).(\\d).(\\d).(\\d+)");
  QRegularExpressionMatch result = re.match(values[index]);
  if(result.hasMatch())
  {
    QString ver = result.captured(1)+"."+result.captured(2)+"."+result.captured(3)+" ("+result.captured(4)+")";
    m_version = ver.toStdString();
  }

  if(m_version.empty())
    LOG_ERROR << "Fail to grab game version info in .build.info file";
  else
    LOG_INFO << "Version successfully found :" << m_version.c_str();

}

void CASCFolder::initFileList(std::set<FileTreeItem> &dest)
{
	QFile file("listfile.txt");
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		LOG_ERROR << "Fail to open listfile.txt.";
		return;
	}

	QTextStream in(&file);

	while (!in.atEnd())
	{
		QString line = in.readLine();

		QString fileName = line.section('\\',-1).toLower();
		QString filePath = (line.section('\\', 0, -2).toLower()) + "\\";
		folderFileList.push_back(std::make_pair(filePath, fileName));

		FileTreeItem tmp;

		line = line.toLower();
		QString firstLetter = line[0];
		firstLetter = firstLetter.toUpper();
		line[0] = firstLetter[0];
		int ret = line.indexOf('\\');
		if (ret>-1)
		{
			firstLetter = line[ret+1];
			firstLetter = firstLetter.toUpper();
			line[ret+1] = firstLetter[0];
		}

		// fill global map for path search
		fileName = line.section('\\',-1).toLower();
		globalNameMap[fileName] = line;

		tmp.displayName = line;
		tmp.color = 0;
		dest.insert(tmp);
  }
}

void CASCFolder::filterFileList(std::set<FileTreeItem> &dest, bool filterfunc(QString)/* = CASCFolder::defaultFilterFunc*/)
{
	std::map<QString,QString>::iterator itEnd = globalNameMap.end();
	for(std::map<QString,QString>::iterator it = globalNameMap.begin();
			it != itEnd;
			++it)
	{
		if(filterfunc(it->second))
		{
			FileTreeItem tmp;
			tmp.displayName = it->second;
			tmp.color = 0;
			dest.insert(tmp);
		}
	}
}

QString CASCFolder::getFullPathForFile(QString file)
{
  std::map<QString,QString>::iterator it = globalNameMap.find(file.toLower());

  if(it != globalNameMap.end())
  	return it->second;

  return "";
}

void CASCFolder::getFilesForFolder(std::vector<QString> &fileNames, QString folderPath)
{
  std::vector<std::pair<QString,QString>> matches;

  // make a new vector array with only those pairs that match the supplied folder path:
  std::copy_if(folderFileList.begin(), folderFileList.end(), back_inserter(matches),
               [folderPath](std::pair<QString,QString>& p)
                  { return (QString::compare(p.first, folderPath, Qt::CaseInsensitive) == 0); });

  // Convert to a simpler vector array of full file names:
  std::transform(matches.begin(), matches.end(), std::back_inserter(fileNames),
                 [](std::pair<QString, QString>& p) { return p.first + p.second; });
}

bool CASCFolder::fileExists(std::string file)
{
  //LOG_INFO << __FUNCTION__ << " " << file.c_str();
  if(!hStorage)
    return false;

  HANDLE dummy;

  if(CascOpenFile(hStorage,file.c_str(), m_currentCascLocale, 0, &dummy))
  {
   // LOG_INFO << "OK";
    CascCloseFile(dummy);
    return true;
  }

 // LOG_ERROR << "Opening" << file.c_str() << "failed." << "Error" << GetLastError();
  return false;
}
