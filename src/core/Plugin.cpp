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
#include <iostream>

// Qt 
#include <QCoreApplication>
#include <QPluginLoader>
#include <QThread>

// Externals

// Other libraries

// Current library

// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================
core::GlobalSettings * Plugin::globalSettings_ = nullptr;

QCoreApplication * Plugin::app_ = nullptr;
QThread * Plugin::thread_ = nullptr;


// Constructors 
//--------------------------------------------------------------------
Plugin::Plugin()
 : Component(), internalName_(""), category_(""), version_(""), coreVersionNeeded_("")
{

}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
// Private Qt application
Plugin * Plugin::load(const std::string & path, core::GlobalSettings & settings)
{
  const auto pluginToLoad = QString::fromStdString(path);
  Plugin * newPlugin = nullptr;

  QPluginLoader loader(pluginToLoad);

  if(auto* const plugin = loader.instance())
  {
    newPlugin = dynamic_cast<Plugin *>(plugin);
    const auto metaInfos = loader.metaData().value("MetaData").toObject();
    newPlugin->setName(metaInfos.value("name").toString());
    newPlugin->version_ = metaInfos.value("version").toString().toStdString();
    newPlugin->coreVersionNeeded_ = metaInfos.value("coreVersion").toString().toStdString();
    newPlugin->internalName_ = metaInfos.value("internalname").toString().toStdString();
    newPlugin->category_ = metaInfos.value("category").toString().toStdString();

    transmitSingletonsFromCore(settings);

    // waiting for the overall application being a Qt application, we start a QCoreApplication in a dedicated
    // thread for each plugin, so that Qt event loop is accessible from plugins (see onExec slot that actually
    // starts the app)
    Plugin::thread_ = new QThread();
    connect(Plugin::thread_, SIGNAL(started()), newPlugin, SLOT(onExec()), Qt::DirectConnection);
    Plugin::thread_->start();
  }
  else
  {
    LOG_ERROR << "Unable to load plugin file " << pluginToLoad << ": " << loader.errorString();
  }
  return newPlugin;
}

void Plugin::doPrint(const QString & prefix)
{
  LOG_INFO << prefix << QString::fromStdString(id()) << ": " << name() << " (version: " << QString::fromStdString(version_) << " - core needed: " << QString::fromStdString(coreVersionNeeded_) << ")";
}

// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
void Plugin::transmitSingletonsFromCore(core::GlobalSettings & settings)
{
  Plugin::globalSettings_ = &settings;
}

void Plugin::onExec()
{
  if (QCoreApplication::instance() == nullptr)
  {
    auto argc = 1;
    char * argv[] = {"plugin.app", nullptr};
    app_ = new QCoreApplication(argc,argv);
    if(app_)
      app_->exec();
    delete app_;
  }
}
