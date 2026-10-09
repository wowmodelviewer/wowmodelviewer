/*
 * CASCFolder.cpp
 *
 *  Created on: 22 oct. 2014
 *      Author: Jeromnimo
 */

#include "CASCFolder.h"

#ifndef __CASCLIB_SELF__
  #define __CASCLIB_SELF__
#endif
#include "CascLib.h"

#include <locale>
#include <map>
#include <utility>

#include <QFile>
#include <QRegularExpression>

#include "CASCFile.h"
#include "logger/Logger.h"

CASCFolder::CASCFolder()
 : m_currentCascLocale(CASC_LOCALE_NONE), m_folder(""), m_openError(ERROR_SUCCESS), hStorage(nullptr)
{

}

void CASCFolder::init(const QString &path)
{
  m_folder = path;

  if(m_folder.endsWith("\\"))
    m_folder.remove(m_folder.size()-1,1);

  initBuildInfo();
}

bool CASCFolder::setConfig(core::GameConfig config)
{
  // The requested config only becomes the current one once its storage has opened: a client that fails to open
  // (its game data not installed, say) leaves the one already loaded as it was, rather than half replaced.
  const core::GameConfig previousConfig = m_currentConfig;
  m_currentConfig = config;

  // init map based on CASCLib
  std::map<QString, int> locales;
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

  // set locale
  if (!m_currentConfig.locale.isEmpty())
  {
    auto it = locales.find(m_currentConfig.locale);

    if (it != locales.end())
    {
      HANDLE dummy;
      // CascLib's storage param uses CASC_PARAM_SEPARATOR ('*'), NOT ':', to split the
      // local path from the product code name (e.g. "...\Data*wowt"). Passing ':' left the
      // code name unset, so CascLib fell back to the FIRST active product in .build.info
      // (retail "wow") for every product -- silently loading the retail root instead of the
      // requested one (e.g. the PTR "wowt" root), which hid PTR-only files like new creatures.
      QString cascParams = m_folder + "*" + m_currentConfig.product;
      LOG_INFO << "Loading Game Folder:" << cascParams;
      // locale found => try to open it, into a handle of its own: the previous storage stays in use if it fails.
      HANDLE opened = nullptr;
      if (!CascOpenStorage(cascParams.toStdWString().c_str(), it->second, &opened))
      {
        m_openError = GetLastError();
        LOG_ERROR << "CASCFolder: Opening" << cascParams << "failed." << "Error" << m_openError;
        m_currentConfig = previousConfig;
        return false;
      }
      m_openError = ERROR_SUCCESS;
      // Loading a client again opens its storage afresh, so let go of the previous one instead of
      // keeping both in memory. A file still open on it keeps it alive until that file is closed.
      if (hStorage)
        CascCloseStorage(hStorage);
      hStorage = opened;

      addExtraEncryptionKeys();

      // Trust the locale chosen from .build.info and resolve files by FileDataID
      // (see fileExists()/openFile(), which use CASC_OPEN_BY_FILEID with this
      // locale). Modern WoW roots (BfA+) carry no filename hashes, so opening a
      // file BY NAME always fails even with a perfectly valid locale -- the old
      // "Localization.lua" probe below therefore must NOT be treated as fatal.
      m_currentCascLocale = it->second;

      if (CascOpenFile(hStorage, "Interface\\FrameXML\\Localization.lua", it->second, 0, &dummy))
      {
        CascCloseFile(dummy);
        LOG_INFO << "Locale set (legacy name probe ok):" << m_currentConfig.locale;
      }
      else
      {
        LOG_INFO << "Locale set from .build.info (name probe unavailable, normal on modern WoW):" << m_currentConfig.locale;
      }

      // Index every present FileDataID up front so fileExists() -- called ~2.17M times while
      // parsing the listfile -- is an O(1) set lookup instead of a per-id CascOpenFile +
      // CascCloseFile round-trip (that probing was ~6.5s of the startup freeze).
      buildPresentIdIndex();
      return true;
    }
  }

  // No locale to open it with (none in .build.info for this product, or one CascLib does not know).
  LOG_ERROR << "CASCFolder: no usable locale for" << m_currentConfig.product << m_currentConfig.version
            << "(locale" << m_currentConfig.locale << ") - not opened";
  m_currentConfig = previousConfig;
  m_openError = ERROR_INVALID_PARAMETER;
  return false;
}

void CASCFolder::closeStorage()
{
  if (hStorage)
    CascCloseStorage(hStorage);
  hStorage = nullptr;
  m_presentIds.clear();
  m_remoteIds.clear();
}

void CASCFolder::initBuildInfo()
{
  m_configs.clear();
  QString buildinfofile = m_folder + "\\..\\.build.info";
  LOG_INFO << "buildinfofile : " << buildinfofile;

  QFile file(buildinfofile);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    LOG_ERROR << "Fail to open .build.info to grab game config info";
    return;
  }

  QTextStream in(&file);
  QString line;

  // read first line and grab VERSION index
  line = in.readLine();

  QStringList headers = line.split('|');
  int activeIndex = 0;
  int versionIndex = 0;
  int tagIndex = 0;
  int productIndex = 0;
  for (int index = 0; index < headers.size(); index++)
  {
    if (headers[index].contains("Active", Qt::CaseInsensitive))
      activeIndex = index;
    else if (headers[index].contains("Version", Qt::CaseInsensitive))
      versionIndex = index;
    else if (headers[index].contains("Tags", Qt::CaseInsensitive))
      tagIndex = index;
    else if (headers[index].contains("Product", Qt::CaseInsensitive))
      productIndex = index;
  }

  // now loop across file lines with actual values
  while (in.readLineInto(&line))
  {
    QString version, product;
    QStringList values = line.split('|');

    // if inactive config, skip it
    if (values[activeIndex] == "0")
      continue;

    // grab version for this line
    QRegularExpression re("^(\\d+).(\\d+).(\\d+).(\\d+)$");
    QRegularExpressionMatch result = re.match(values[versionIndex]);
    if (result.hasMatch())
      version = result.captured(1) + "." + result.captured(2) + "." + result.captured(3) + "." + result.captured(4);

    // grab product name for this line
    product = values[productIndex];

    // grab locale(s) for this line
    values = values[tagIndex].split(':');
    for (int i = 0; i < values.size(); i++)
    {
      if (values[i].contains("text?"))
      {
        QStringList tags = values[i].split(" ");
        core::GameConfig config;
        config.locale = tags[tags.size() - 2];
        config.version = version;
        config.product = product;
        m_configs.push_back(config);
      }
    }

  }

  for (auto it : m_configs)
    LOG_INFO << "config" << it.locale << it.version;
}


void CASCFolder::buildPresentIdIndex()
{
  m_presentIds.clear();
  m_remoteIds.clear(); // before any early return: a reused folder must not keep the previous product's answers
  if (!hStorage)
    return;

  CASC_FIND_DATA fd;
  HANDLE hFind = CascFindFirstFile(hStorage, "*", &fd, NULL);
  if (hFind == NULL || hFind == INVALID_HANDLE_VALUE)
  {
    LOG_INFO << "CASCFolder: storage enumeration unavailable; fileExists() will probe per id.";
    return; // leave m_presentIds empty -> fileExists() falls back to the per-id probe
  }

  m_presentIds.reserve(1u << 21); // ~2M files in a modern retail build

  // Progress reporting: the enumeration count isn't known up front, so report a soft fraction
  // against a rough expected total (~4M files in a current retail build) capped below 1.0, just
  // so the loading bar visibly advances during this multi-second step instead of sitting still.
  size_t seen = 0, nextReport = 0;
  const float EXPECTED = 4000000.0f;

  do
  {
    if (m_progressCb && ++seen >= nextReport)
    {
      const float frac = (float)seen / EXPECTED;
      m_progressCb(frac < 0.99f ? frac : 0.99f);
      nextReport = seen + 50000; // ~80 updates over a full enumeration
    }
    // Index the FileDataIDs that have a copy on this computer (any of their locale entries). A file
    // with none cannot be read from a local storage -- CascLib opens it as an empty file -- so listing
    // it would offer models that never load. Measured 2026-10-06: Retail 12.1 has 586 such ids
    // (cinematics, shader packs; every named model, texture, skin and table is local), Classic Era
    // 1.15.9 40,811 (3,989 models, 25,678 textures: a partly downloaded install), Classic Beta 1.60.1
    // 97,968 (mostly textures). Those are counted (remoteFileCount) rather than indexed.
    if (fd.dwFileDataId != CASC_INVALID_ID)
    {
      const int id = static_cast<int>(fd.dwFileDataId);
      if (fd.bFileAvailable)
      {
        m_presentIds.insert(id);
        m_remoteIds.erase(id);
      }
      else if (!m_presentIds.count(id))
        m_remoteIds.insert(id);
    }
  } while (CascFindNextFile(hFind, &fd));
  CascFindClose(hFind);

  LOG_INFO << "CASCFolder: indexed" << (unsigned int)m_presentIds.size() << "present FileDataIDs (single enumeration);"
           << (unsigned int)m_remoteIds.size() << "more are in the build but not on this computer.";
}

bool CASCFolder::fileExists(int id)
{
  if(!hStorage)
    return false;

  // Fast path: O(1) lookup in the enumerated id set (buildPresentIdIndex). A real model
  // load still force-opens by id (WoWFolder::getFile), so any id missed by enumeration is
  // still loadable -- it just won't appear in the browse tree.
  if (!m_presentIds.empty())
    return m_presentIds.count(id) != 0;

  // Fallback (enumeration unavailable): the original per-id open/close probe.
  HANDLE dummy;
  if(CascOpenFile(hStorage, CASC_FILE_DATA_ID(id), m_currentCascLocale, CASC_OPEN_BY_FILEID, &dummy))
  {
    CascCloseFile(dummy);
    return true;
  }
  return false;
}

bool CASCFolder::openFile(int id, HANDLE * result)
{
  return CascOpenFile(hStorage, CASC_FILE_DATA_ID(id), m_currentCascLocale, CASC_OPEN_BY_FILEID, result);
}

bool CASCFolder::closeFile(HANDLE file)
{
  return CascCloseFile(file);
}

void CASCFolder::addExtraEncryptionKeys()
{
  QFile tactKeys("extraEncryptionKeys.csv");

  if (tactKeys.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    QTextStream in(&tactKeys);
    while (!in.atEnd())
    {
      QString line = in.readLine();
      if (line.startsWith("##") || line.startsWith("\"##"))  // ignore lines beginning with ##, useful for adding comments.
        continue;
        
      QStringList lineData = line.split(';');
      if (lineData.size() != 2)
        continue;
      QString keyName = lineData.at(0);
      QString keyValue = lineData.at(1);
      if (keyName.isEmpty() || keyValue.isEmpty())
        continue;

      bool ok, ok2;
      ok2 = CascAddStringEncryptionKey(hStorage, keyName.toULongLong(&ok, 16), keyValue.toStdString().c_str());
      if (!ok2)
          LOG_ERROR << "Failed to add TACT key from file, Name:" << keyName << ", Value:" << keyValue;
    }
  }
}

/*
int CASCFolder::fileDataId(std::string & filename)
{
  return CascGetFileId(hStorage, filename.c_str());
}
*/
