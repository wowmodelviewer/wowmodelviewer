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
 * Observable.cpp
 *
 *  Created on: 3 january 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _OBSERVABLE_CPP_
#include "Observable.h"
#undef _OBSERVABLE_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries

// Current library
#include "Event.h"
#include "Observer.h"

// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------


// Constructors
//--------------------------------------------------------------------
Observable::Observable()
{
  m_observerList.clear();
}

// Destructor
//--------------------------------------------------------------------
Observable::~Observable()
{
  // we send an event to observers attached in order to react to observable destruction
  Event l_event(this,Event::DESTROYED);
  notify(l_event);

  // we remove
  for(std::list<Observer *>::iterator l_it=m_observerList.begin();l_it!=m_observerList.end();)
  {
    (*l_it)->removeObservable(this);
    l_it = m_observerList.erase(l_it);
  }
}

// Public methods
//--------------------------------------------------------------------
void Observable::attach(Observer * observer)
{
  if(observerAttached(observer) == m_observerList.end())
  {
    m_observerList.push_back(observer);
    observer->addObservable(this);
  }

}

void Observable::detach(Observer * observer)
{
  std::list<Observer *>::iterator l_it = observerAttached(observer);

  if(l_it != m_observerList.end())
  {
    m_observerList.erase(l_it);
    observer->removeObservable(this);
  }
}

// Protected methods
//--------------------------------------------------------------------
void Observable::notify(Event & event)
{
  std::list<Observer *>::iterator l_it;

  for(l_it = m_observerList.begin() ; l_it != m_observerList.end() ; l_it++)
  {
    (*l_it)->treatEvent(&event);
  }
}


// Private methods
//--------------------------------------------------------------------
std::list<Observer *>::iterator Observable::observerAttached(Observer * observer)
{
  std::list<Observer *>::iterator l_result = m_observerList.end();
    if(observer != 0)
  {
    std::list<Observer *>::iterator l_it;
    for(l_it = m_observerList.begin() ; l_it != m_observerList.end() ; l_it++)
    {
            if( (*l_it) == observer)
      {
        l_result = l_it;
        break;
      }
    }
  }
  return l_result;
}
