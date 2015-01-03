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
 *  Created on: 3 january 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _EVENT_H_
#define _EVENT_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries

// Current library
class Observable;

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class Event
{
	public :
		// Constants / Enums
		enum EventType
		{
      DESTROYED = 0xFFFFFF00
		};


		// Constructors
		Event(Observable *, EventType);

		// Destructors
		~Event();

		// Methods
		EventType type() { return m_type; }

		Observable * sender() { return m_p_sender; }

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
		EventType m_type;
		Observable * m_p_sender;

		// friend class declarations
};

// static members definition
#ifdef _EVENT_CPP_

#endif

#endif /* _EVENT_H_ */
