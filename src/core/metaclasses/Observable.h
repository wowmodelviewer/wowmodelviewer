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
 * Observable.h
 *
 *  Created on: 3 january 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _OBSERVABLE_H_
#define _OBSERVABLE_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <list>

// Qt

// Externals

// Other libraries

// Current library
#include "Event.h"

class Observer;

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _OBSERVABLE_API_ __declspec(dllexport)
#    else
#        define _OBSERVABLE_API_ __declspec(dllimport)
#    endif
#else
#    define _OBSERVABLE_API_
#endif

class _OBSERVABLE_API_ Observable
{
  public :
    // Constants / Enums

    // Constructors
    Observable();

    // Destructors
    virtual ~Observable();

    // Methods
    void attach(Observer *);

    void detach(Observer *);

  protected :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods
    void notify(Event &);

    // Members

  private :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods
    std::list<Observer *>::iterator observerAttached(Observer *);

    // Members
    std::list<Observer *> m_observerList;

    // friend class declarations
};

// static members definition
#ifdef _OBSERVABLE_CPP_

#endif

#endif /* _OBSERVABLE_H_ */
