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
 * Plugin.cpp
 *
 *  Created on: 24 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _PLUGIN_CPP_
#include "Plugin.h"
#undef _PLUGIN_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt 
#include <QCoreApplication>
#include <QPluginLoader>
#include <QThread>

// Externals

// Other libraries

// Current library
#include "PluginManager.h"

// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================
GlobalSettings * Plugin::globalSettings = 0;
QCoreApplication * Plugin::app = NULL;
QThread * Plugin::thread = NULL;


// Constructors 
//--------------------------------------------------------------------
Plugin::Plugin()
 : Component(), m_internalName(""), m_category(""), m_version(""), m_coreVersionNeeded("")
{

}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
// Private Qt application
Plugin * Plugin::load(std::string path, GlobalSettings & settings)
{
  QString pluginToLoad = QString::fromStdString(path);
  Plugin * newPlugin = NULL;

  QPluginLoader loader(pluginToLoad);

  if(QObject * plugin = loader.instance())
  {
    newPlugin = dynamic_cast<Plugin *>(plugin);
    QJsonObject metaInfos = loader.metaData().value("MetaData").toObject();
    newPlugin->setName(metaInfos.value("name").toString());
    newPlugin->m_version = metaInfos.value("version").toString().toStdString();
    newPlugin->m_coreVersionNeeded = metaInfos.value("coreVersion").toString().toStdString();
    newPlugin->m_internalName = metaInfos.value("internalname").toString().toStdString();
    newPlugin->m_category = metaInfos.value("category").toString().toStdString();

    newPlugin->transmitGlobalsFromCore(settings);

    // waiting for the overall application being a Qt application, we start a QCoreApplication in a dedicated
    // thread for each plugin, so that Qt event loop is accessible from plugins (see onExec slot that actually
    // starts the app)
    Plugin::thread = new QThread();
    connect(Plugin::thread, SIGNAL(started()), newPlugin, SLOT(onExec()), Qt::DirectConnection);
    Plugin::thread->start();

    return newPlugin;
  }
  else
  {
    std::cout << "Unable to load plugin file " << path << ":" << loader.errorString().toStdString() << std::endl;
  }
  return newPlugin;
}

void Plugin::doPrint()
{
  std::cout << id() << ": " << name().toStdString() << " (version: " << m_version << " - core needed: " << m_coreVersionNeeded << ")" << std::endl;
}

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
void Plugin::transmitGlobalsFromCore(GlobalSettings & settings)
{
  Plugin::globalSettings = &settings;
}

void Plugin::onExec()
{
  if (QCoreApplication::instance() == NULL)
  {
    int argc = 1;
    char * argv[] = {"plugin.app", NULL};
    Plugin::app = new QCoreApplication(argc,argv);
    Plugin::app->exec();
    if (Plugin::app)
      delete Plugin::app;
  }
}
