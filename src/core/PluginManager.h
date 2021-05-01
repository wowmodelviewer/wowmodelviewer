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
 * PluginManager.h
 *
 *  Created on: 24 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _PLUGINMANAGER_H_
#define _PLUGINMANAGER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries
#include "metaclasses/Container.h"
#include "Plugin.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
#ifndef BUILDING_PLUGIN
  #define PLUGINMANAGER PluginManager::instance()
#endif

class PluginManager : public Container<Plugin>
{
  public :
    // Constants / Enums
    
    // Constructors

    // Destructors
  
    // Methods
    static PluginManager & instance()
    {
      if(PluginManager::instance_ == nullptr)
        PluginManager::instance_ = new PluginManager();

      return *instance_;
    }

    // load all plugins in given directory
    void init(const std::string & dir);

    // overloaded from Component method
    void doPrint(const QString &) override;

    // Members
    
  protected :
    // Constants / Enums
  
    // Constructors
  
    // Destructors
  
    // Methods
    
    // Members
    
  private :
    // Constants / Enums
  
    // Constructors
    // to avoid unwanted instantiation as this class is a Singleton
    PluginManager();
    PluginManager(PluginManager &);

    // Destructors
  
    // Methods

    // Members
    static PluginManager * instance_;

    // friend class declarations
  
};

// static members definition
#ifdef _PLUGINMANAGER_CPP_

#endif

#endif /* _PLUGINMANAGER_H_ */
