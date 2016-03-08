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
#include "GlobalSettings.h"
#include "logger/Logger.h"
#include "metaclasses/Component.h"

// Current library

// Namespaces used
//--------------------------------------------------------------------

// Class Declaration
//--------------------------------------------------------------------
#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _PLUGIN_API_ __declspec(dllexport)
#    else
#        define _PLUGIN_API_ __declspec(dllimport)
#    endif
#else
#    define _PLUGIN_API_
#endif

class _PLUGIN_API_ Plugin : public QObject, public Component
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
    std::string coreVersionNeeded() const { return m_coreVersionNeeded;}
    std::string version() const { return m_version; }
    std::string id() const { return (m_category + "_" + m_internalName); }

    static Plugin * load(std::string path, GlobalSettings &);

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
    void transmitGlobalsFromCore(GlobalSettings &);

		// Members
    std::string m_internalName;
    std::string m_category;
    std::string m_version;
    std::string m_coreVersionNeeded;
		
    static GlobalSettings * globalSettings;
    static QCoreApplication * app;
    static QThread * thread;

		// friend class declarations
    friend class GlobalSettings;

  private slots:
    void onExec();
};

// static members definition
#ifdef _PLUGIN_CPP_

#endif

#endif /* _PLUGIN_H_ */
