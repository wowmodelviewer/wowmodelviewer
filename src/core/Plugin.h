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
 * Plugin.h
 *
 *  Created on: 24 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _PLUGIN_H_
#define _PLUGIN_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <string>

// Qt
class QCoreApplication;
class QThread;

// Externals
class ModelViewer;

// Other libraries
#include "Game.h"
#include "GlobalSettings.h"
#include "Logger.h"
#include "metaclasses/Component.h"

// Current library

// Namespaces used
//--------------------------------------------------------------------

// Class Declaration
//--------------------------------------------------------------------
class Plugin : public QObject, public Component
{
    Q_OBJECT
  public :
    // Constants / Enums
    
    // Constructors
    Plugin();

    // Destructors
    ~Plugin() {}
  
    // Methods
    // these fields are filled within json plugin informations and set by PluginManager
    // at load time
    std::string coreVersionNeeded() const { return coreVersionNeeded_;}
    std::string version() const { return version_; }
    std::string id() const { return (category_ + "_" + internalName_); }

    static Plugin * load(std::string path, core::GlobalSettings &, core::Game &);

    // access to singleton passed at load time
    static core::GlobalSettings & globalSettings() { return *globalSettings_; }

    // overload from component class
    void doPrint();

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
  
    // Destructors
  
    // Methods
    void transmitSingletonsFromCore(core::GlobalSettings &, core::Game &);

    // Members
    std::string internalName_;
    std::string category_;
    std::string version_;
    std::string coreVersionNeeded_;

    static QCoreApplication * app_;
    static QThread * thread_;

    // Singletons from core application
    static core::GlobalSettings * globalSettings_;
    static core::Game * game_;

  private slots:
    void onExec();
};

// static members definition
#ifdef _PLUGIN_CPP_

#endif

#endif /* _PLUGIN_H_ */
