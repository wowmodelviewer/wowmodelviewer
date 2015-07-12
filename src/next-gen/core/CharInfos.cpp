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
 * CharInfos.cpp
 *
 *  Created on: 12 July 2015
 *      Author: Jeromnimo
 */

#define _CHARINFOS_CPP_
#include "CharInfos.h"
#undef _CHARINFOS_CPP_


// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries

// Current library
#include "logger/Logger.h"

// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================

// Constructors
//--------------------------------------------------------------------


// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------


// Protected methods
//--------------------------------------------------------------------
CharInfos::CharInfos()
: raceId(0), genderId(0), race(""), gender(""), hasTransmogGear(false), skinColor(0),
  faceType(0), hairColor(0), hairStyle(0), facialHair(0), eyeGlowType(0),
  tabardIcon(-1), IconColor(-1), tabardBorder(-1), BorderColor(-1), Background(-1)
{
  for(unsigned int i=0; i < NUM_CHAR_SLOTS ; i++)
    equipment[i] = 0;
}

// Private methods
//--------------------------------------------------------------------



