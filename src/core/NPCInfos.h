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
 * NPCInfos.h
 *
 *  Created on: 26 nov. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _NPCINFOS_H_
#define _NPCINFOS_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <string>

// Qt

// Externals

// Other libraries

// Current library


// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _NPCINFOS_API_ __declspec(dllexport)
#    else
#        define _NPCINFOS_API_ __declspec(dllimport)
#    endif
#else
#    define _NPCINFOS_API_
#endif

class _NPCINFOS_API_ NPCInfos
{
  public :
    // Constants / Enums

    // Constructors
    NPCInfos();

    ~NPCInfos() {}
    // Destructors

    // Methods

    // Members
    int id;
    int displayId;
    int type;
    std::string name;

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
#ifdef _NPCINFOS_CPP_

#endif
#endif /* _NPCINFOS_H_ */
