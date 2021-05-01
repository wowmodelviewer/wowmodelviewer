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
 * PluginManager.cpp
 *
 *  Created on: 24 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _PLUGINMANAGER_CPP_
#include "PluginManager.h"
#undef _PLUGINMANAGER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <iostream>

// Qt 
#include <QDir>
#include <QString>
#include <QStringList>


// Externals

// Other libraries

// Current library
#include "GlobalSettings.h"

// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================
PluginManager * PluginManager::instance_ = nullptr;

// Constructors 
//--------------------------------------------------------------------
// private constructor
PluginManager::PluginManager()
{

}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
 void PluginManager::init(const std::string & dir)
 {
   const auto directory = QString::fromStdString(dir);
   const QDir pluginDir(directory);
   auto plugins = pluginDir.entryList(QDir::Files);
   for (auto i=0;i<plugins.size();i++)
   {
     if (plugins.at(i).contains(".dll") == false) continue;   // Skip non-plugin files
     auto* const newPlugin = Plugin::load(pluginDir.absoluteFilePath(plugins[i]).toStdString(),GLOBALSETTINGS);
     if(newPlugin)
       addChild(newPlugin);
   }
   print();
 }

 void PluginManager::doPrint(const QString & prefix)
 {
   LOG_INFO << prefix << "PluginManager (" << nbChildren() << " plugins loaded)";
 }

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
