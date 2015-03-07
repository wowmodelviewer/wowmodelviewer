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
 * UpdateManager.cpp
 *
 *  Created on: 31 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _UPDATEMANAGER_CPP_
#include "UpdateManager.h"
#undef _UPDATEMANAGER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QDesktopServices>
#include <QGroupBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVboxLayout>

// Externals

// Other libraries
#include "core/FileDownloader.h"
#include "core/GlobalSettings.h"
#include "core/PluginManager.h"
#include "core/VersionManager.h"

#include "logger/Logger.h"
#include "logger/LogOutputFile.h"

#include "metaclasses/Iterator.h"


// Current library


// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================

// Constructors 
//--------------------------------------------------------------------
UpdateManager::UpdateManager()
{
  LOGGER.addChild(new WMVLog::LogOutputFile("userSettings/updateManagerLog.txt"));

  PLUGINMANAGER.init("./plugins");

  // create main table
  m_table = new QTableWidget(0, 4, this);
  QStringList labels("Name");
  labels.append("Current Version");
  labels.append("Last Version");
  labels.append("State");
  m_table->setHorizontalHeaderLabels(labels);

  // fill main layout
  QVBoxLayout * layout = new QVBoxLayout;
  layout->addWidget(m_table);
  QWidget * container = new QWidget;
  container->setLayout(layout);
  setCentralWidget(container);

  // init download manager
  m_fileDownloader = new FileDownloader(this);
  connect(m_fileDownloader, SIGNAL(downloadFinished(QString &)),
          this, SLOT(fileDownloaded(QString &)));

  m_versionManager = new VersionManager(this);
  connect(m_versionManager, SIGNAL(downloadFinished(QString &)),
      this, SLOT(fileDownloaded(QString &)));

  connect(m_table, SIGNAL(cellClicked(int, int)), this, SLOT(cellClicked(int, int)));

  updateTable();
}

// Destructor
//--------------------------------------------------------------------
UpdateManager::~UpdateManager()
{
  delete m_fileDownloader;
}

// Public methods
//--------------------------------------------------------------------

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
void UpdateManager::updateTable()
{
  int row = 0;
  int column = 0;
  std::map<QString,QString>::iterator versionsIt = m_versionManager->m_currentVersionsMap.begin();
  std::map<QString,QString>::iterator versionsItEnd = m_versionManager->m_currentVersionsMap.end();

  for( ; versionsIt != versionsItEnd ; versionsIt++)
  {
    if((*versionsIt).first == QString(GLOBALSETTINGS.appName().c_str()))
      break;
  }

  // insert core version first
  m_table->insertRow(row);
  QTableWidgetItem *newItem = new QTableWidgetItem(tr("%1").arg((*versionsIt).first));
  m_table->setItem(row, column, newItem);
  column++;
  newItem = new QTableWidgetItem(tr("%1").arg((*versionsIt).second));
  newItem->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
  m_table->setItem(row, column, newItem);
  column++;
  newItem = new QTableWidgetItem(tr("N/A"));
  newItem->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
  m_table->setItem(row, column, newItem);
  row++;

  // now fill all other stuff
  for(versionsIt = m_versionManager->m_currentVersionsMap.begin() ; versionsIt != versionsItEnd ; versionsIt++)
  {
    column = 0;
    // core version already inserted, skip it
    if((*versionsIt).first == QString(GLOBALSETTINGS.appName().c_str()))
      continue;

    m_table->insertRow(row);
    QTableWidgetItem *newItem = new QTableWidgetItem(tr("%1").arg((*versionsIt).first));
    m_table->setItem(row, column, newItem);
    column++;
    newItem = new QTableWidgetItem(tr("%1").arg((*versionsIt).second));
    newItem->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
    m_table->setItem(row, column, newItem);
    column++;
    newItem = new QTableWidgetItem(tr("N/A"));
    newItem->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
    m_table->setItem(row, column, newItem);
    row++;
    m_table->resizeColumnsToContents();
  }

  resizeTable();
}

void UpdateManager::fileDownloaded(QString & filename)
{
  if(filename.contains("latest.json"))
  {
    updateLastVersionInfo();
  }
}

void UpdateManager::updateLastVersionInfo()
{
  for(unsigned int i=0; i < m_versionManager->m_lastVersionInfos.size() ; i++)
  {
    for(int row=0 ; row < m_table->rowCount() ; row++)
    {
      if(m_table->item(row,0)->text() == m_versionManager->m_lastVersionInfos[i]["name"].toString())
      {
        m_table->item(row,2)->setText(m_versionManager->m_lastVersionInfos[i]["version"].toString());
        if(VersionManager::compareVersion(m_table->item(row,1)->text(),m_table->item(row,2)->text()) < 0)
        {
          QTableWidgetItem *newItem = new QTableWidgetItem(tr("Update Needed"));
          newItem->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
          m_table->setItem(row, 3, newItem);
        }
        else
        {
          QTableWidgetItem *newItem = new QTableWidgetItem(tr("Up to date"));
          newItem->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
          m_table->setItem(row, 3, newItem);
        }
        m_table->resizeColumnsToContents();
      }
    }
  }
  resizeTable();
}

void UpdateManager::resizeTable()
{
  //get all columns width and resize window accordingly
  int finalWidth = 0;
  for(int i = 0 ; i < m_table->columnCount() ; i++)
    finalWidth += m_table->columnWidth(i);

  finalWidth += 50; // make sure everything is visible (lines, margins, etc)

  setMinimumSize(finalWidth, 0);
}

void UpdateManager::cellClicked(int row, int col)
{
  if(m_table->item(row,col) && m_table->item(row,col)->text() == "Update Needed")
    QDesktopServices::openUrl(QUrl("https://wowmodelviewer.atlassian.net/wiki/display/WMV/Download+WoW+Model+Viewer"));
}
