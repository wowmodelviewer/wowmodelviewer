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
 * main.cpp
 *
 *  Created on: 31 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTableWidget>
#include <QVboxLayout>

// Externals

// Other libraries
#include <iostream>
#include <string>
#include "FileDownloader.h"
#include "VersionManager.h"
#include "UpdateManager.h"

int main(int argc, char ** argv)
{
  bool uiversion = true;
  for(int i=0; i < argc ; i++)
  {
    if(std::string(argv[i]) == "--no-ui")
    {
      uiversion = false;
    }
  }

  if(uiversion)
  {
    QApplication app(argc, argv);
    UpdateManager mainWindow;
    mainWindow.show();
    return app.exec();
  }
  else
  {
    QCoreApplication app(argc, argv);
    VersionManager manager;
    return app.exec();
  }
}
