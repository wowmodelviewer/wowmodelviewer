/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/


/*
 * VersionManager.cpp
 *
 *  Created on: 16 feb. 2014
 *   Copyright: 2014 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _VERSIONMANAGER_CPP_
#include "VersionManager.h"
#undef _VERSIONMANAGER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt 
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// Externals

// Other libraries

// Current library
#include "FileDownloader.h"
#include "GlobalSettings.h"
#include "PluginManager.h"

// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================

// Constructors 
//--------------------------------------------------------------------
VersionManager::VersionManager(QObject * parent)
: QObject(parent), m_standaloneVersion(parent == 0)
{
  m_fileDownloader = new FileDownloader(this);
  connect(m_fileDownloader, SIGNAL(downloadFinished(QString &)),
      this, SLOT(fileDownloaded(QString &)));

  m_fileDownloader->get(QUrl("http://update.wowmodelviewer.net/latest.json"));
  updateCurrentVersionInfo();
}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
void VersionManager::updateCurrentVersionInfo()
{
  // core version
  QString appName(GLOBALSETTINGS.appName().c_str());
  QString appVersion(GLOBALSETTINGS.appVersion().c_str());
  m_currentVersionsMap.insert(std::make_pair(appName,appVersion));

  // init plugins infos
  for(PluginManager::iterator it = PLUGINMANAGER.begin();
      it != PLUGINMANAGER.end() ;
      ++it)
  {
    QString pluginName((*it)->name());
    QString pluginVersion((*it)->version().c_str());
    m_currentVersionsMap.insert(std::make_pair(pluginName,pluginVersion));
  }
}

QString VersionManager::getLastVersionFor(QString & name)
{
  for(unsigned int i=0; i < m_lastVersionInfos.size() ; i++)
  {
    if(m_lastVersionInfos[i]["name"].toString() == name)
    {
      return m_lastVersionInfos[i]["version"].toString();
    }
  }
  return "";
}

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
void VersionManager::checkForNewVersionAndExit()
{
  // only check core version for now
  QString appName = GLOBALSETTINGS.appName().c_str();
  QString currentVersion = m_currentVersionsMap[appName];
  QString lastVersion = getLastVersionFor(appName);
  int result = compareVersion(currentVersion,lastVersion);
  qApp->exit(result);
}

// returns -1 if v1 < v2
// returns 0 if v1 == v2
// returns 1 if v1 > v2
int VersionManager::compareVersion(const QString & v1, const QString & v2)
{
  QStringList v1list = v1.split(".");
  QStringList v2list = v2.split(".");

  int i=0;
  for(; i < v1list.size() ; i++)
  {
    if(i < v2list.size())
    {
      int ver1 = v1list[i].toInt();
      int ver2 = v2list[i].toInt();
      if(ver1 < ver2)
        return -1;
      if(ver1 > ver2)
        return 1;
    }
    else
    {
      // versions equals until last one available in v2, and some remains in v1
      // ==> consider that v1 is higher than v2
      return 1;
    }
  }

  // versions equals until last one available in v1, and some remains in v2
  // ==> consider that v2 is higher than v1
  if(i < v2list.size())
    return -1;

  // perfect equality if we arrive here
  return 0;
}

void VersionManager::fileDownloaded(QString & filename)
{
  if(filename.contains("latest.json"))
  {
    QJsonParseError error;
    QJsonDocument datas = QJsonDocument::fromJson(m_fileDownloader->m_datas, &error);
    QJsonArray values = datas.array();

    for(int i=0; i < values.size() ; i++)
    {
      m_lastVersionInfos.push_back(values[i].toObject());
    }
    emit downloadFinished(filename);

    if(m_standaloneVersion)
    {
      checkForNewVersionAndExit();
    }
   }
}
