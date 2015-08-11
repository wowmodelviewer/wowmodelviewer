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
 * WowheadImporter.h
 *
 *  Created on: 1 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _WOWHEADIMPORTER_H_
#define _WOWHEADIMPORTER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QObject>
#include <QtPlugin>

// Externals

// Other libraries
#define _IMPORTERPLUGIN_CPP_ // to define interface
#include "ImporterPlugin.h"
#undef _IMPORTERPLUGIN_CPP_
// Current library


// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class WowheadImporter : public QObject, public ImporterPlugin
{
    Q_INTERFACES(ImporterPlugin)
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "wowmodelviewer.importers.WowheadImporter" FILE "wowheadimporter.json")

  public :
    // Constants / Enums

    // Constructors
    WowheadImporter() {}

    // Destructors
    ~WowheadImporter() {}

    // Methods
    bool acceptURL(std::string url) const;

    NPCInfos * importNPC(std::string url) const;
    CharInfos * importChar(std::string url) const {return NULL;}
    ItemRecord * importItem(std::string url) const;

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
    std::string extractSubString(std::string & datas, std::string beginPattern, std::string endPattern) const;

    // Members

    // friend class declarations

};

// static members definition
#ifdef _WOWHEADIMPORTER_CPP_

#endif

#endif /* _WOWHEADIMPORTER_H_ */
