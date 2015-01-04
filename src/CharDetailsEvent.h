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
      SKINCOLOR_CHANGED = 0xFFFFF000,
      FACETYPE_CHANGED = 0xFFFFF001,
      HAIRCOLOR_CHANGED = 0xFFFFF002,
      HAIRSTYLE_CHANGED = 0xFFFFF003,
      FACIALHAIR_CHANGED = 0xFFFFF004
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
