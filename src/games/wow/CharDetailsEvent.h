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
 * Event.h
 *
 *  Created on: 4 january 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _CHARDETAILSEVENT_H_
#define _CHARDETAILSEVENT_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries
#include "metaclasses/Event.h"

// Current library

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class CharDetailsEvent : public Event
{
  public :
    // Constants / Enums
    enum EventType
    {
      SKIN_COLOR_CHANGED = 0xFFFFF000,
      FACE_CHANGED = 0xFFFFF001,
      FACIAL_CUSTOMIZATION_STYLE_CHANGED = 0xFFFFF002,
      FACIAL_CUSTOMIZATION_COLOR_CHANGED = 0xFFFFF003,
      ADDITIONAL_FACIAL_CUSTOMIZATION_CHANGED = 0xFFFFF004,
      DH_TATTOO_STYLE_CHANGED = 0xFFFFF005,
      DH_TATTOO_COLOR_CHANGED = 0xFFFFF006,
      DH_HORN_STYLE_CHANGED = 0xFFFFF007,
      DH_BLINDFOLDS_CHANGED = 0xFFFFF008,
      DH_MODE_CHANGED = 0xFFFFF009
    };

	// Constructors
	CharDetailsEvent(Observable * obs, EventType type) : Event(obs, (Event::EventType)type) {}

	// Destructors

	// Methods

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
#ifdef _CHARDETAILSEVENT_CPP_

#endif

#endif /* _CHARDETAILSEVENT_H_ */
