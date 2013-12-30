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
 * ImporterPlugin.h
 *
 *  Created on: 25 nov. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _IMPORTERPLUGIN_H_
#define _IMPORTERPLUGIN_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <string>

// Qt
#include <QObject>

// Externals
struct ItemRecord;
class CharInfos;
class NPCInfos;

// Other libraries

// Current library
#define INCLUDE_FROM_PLUGIN
#include "Plugin.h"
#undef INCLUDE_FROM_PLUGIN

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class ImporterPlugin : public Plugin
{
  public :
    // Constants / Enums

    // Constructors
    ImporterPlugin() {}

    // Destructors

    // Methods
    virtual bool acceptURL(std::string url) const = 0;

    virtual NPCInfos * importNPC(std::string url) const = 0;
    virtual ItemRecord * importItem(std::string url) const = 0;
    virtual CharInfos * importChar(std::string url) const = 0;

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

    // Members

    // friend class declarations

};

// static members definition
#ifdef _IMPORTERPLUGIN_CPP_
Q_DECLARE_INTERFACE(ImporterPlugin,"wowmodelviewer.importerplugin/1.0");
#endif


#endif /* _IMPORTERPLUGIN_H_ */
