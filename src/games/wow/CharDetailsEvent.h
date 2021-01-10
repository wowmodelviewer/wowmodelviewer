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
      DH_MODE_CHANGED = 0x10000000,
      CHOICE_LIST_CHANGED = 0x10000001
    };

  // Constructors
  CharDetailsEvent(Observable * obs, EventType type) : Event(obs, (Event::EventType)type) {}

  // Destructors

  // Methods
  void setCustomizationOptionId(const uint id) { customizationOptionId_ = id; }
  uint getCustomizationOptionId() const { return customizationOptionId_; }


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
    uint customizationOptionId_;

  // friend class declarations
};

// static members definition
#ifdef _CHARDETAILSEVENT_CPP_

#endif

#endif /* _CHARDETAILSEVENT_H_ */
