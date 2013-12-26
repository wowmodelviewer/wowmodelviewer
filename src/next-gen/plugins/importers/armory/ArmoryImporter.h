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
 * ArmoryImporter.h
 *
 *  Created on: 9 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _ARMORYIMPORTER_H_
#define _ARMORYIMPORTER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt
#include <QObject>
#include <QtPlugin>

// Externals

// Other libraries
#include "core/ImporterPlugin.h"

// Current library
#include "wx/jsonreader.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class ArmoryImporter : public QObject, public ImporterPlugin
{
    Q_INTERFACES(ImporterPlugin)
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "wowmodelviewer.importers.WowheadImporter" FILE "armoryimporter.json")

  public :
    // Constants / Enums

    // Constructors
    ArmoryImporter() {}

    // Destructors
    ~ArmoryImporter() {}

    // Methods
    bool acceptURL(std::string url) const;

    NPCInfos * importNPC(std::string url) const {return NULL;};
    CharInfos * importChar(std::string url) const;
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
    enum ImportType
    {
      CHARACTER,
      ITEM
    };

    // Constructors

    // Destructors

    // Methods
    int readJSONValues(ImportType type, std::string url, wxJSONValue & result) const;

    // Members

    // friend class declarations

};

// static members definition
#ifdef _ARMORYIMPORTER_CPP_

#endif

#endif /* _ARMORYIMPORTER_H_ */
