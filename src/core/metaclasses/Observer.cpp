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
 * Observer.cpp
 *
 *  Created on: 3 january 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _OBSERVER_CPP_
#include "Observer.h"
#undef _OBSERVER_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries

// Current library
#include "../core/metaclasses/Event.h"
#include "../core/metaclasses/Observable.h"


// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------


// Constructors
//--------------------------------------------------------------------
Observer::Observer()
{
    m_observableList.clear();
}


// Destructor
//--------------------------------------------------------------------
Observer::~Observer()
{
    std::list<Observable *>::iterator l_it;
    while (!m_observableList.empty())
    {
        l_it = m_observableList.begin();
        (*l_it)->detach(this);
    }
}

// Public methods
//--------------------------------------------------------------------

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
void Observer::treatEvent(Event * event)
{
    if(!event)
        return;

    switch(event->type())
    {
    case Event::DESTROYED:
        onDestroyEvent();
        break;

    default:
        onEvent(event);
        break;
    }
}

void Observer::addObservable(Observable * obs)
{
	std::list<Observable *>::iterator l_toRemove = findObservable(obs);
	if(l_toRemove == m_observableList.end())
	{
		m_observableList.push_back(obs);
	}
}

void Observer::removeObservable(Observable * obs)
{
    m_observableList.erase(findObservable(obs));
}

std::list<Observable *>::iterator Observer::findObservable(Observable * obs)
{
	std::list<Observable *>::iterator l_result = m_observableList.end();

    if(obs != 0)
	{
		std::list<Observable *>::iterator l_it;
		for(l_it = m_observableList.begin() ; l_it != m_observableList.end() ; l_it++)
		{
			if(*l_it == obs)
			{
				l_result = l_it;
				break;
			}
		}
	}

	return l_result;
}
