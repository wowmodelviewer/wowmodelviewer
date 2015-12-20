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
 * Component.cpp
 *
 *  Created on: 23 dec 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _COMPONENT_CPP_
#include "Component.h"
#undef _COMPONENT_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <iostream>
#include <string>
#include <typeinfo>

// Qt 

// Externals

// Other libraries

// Current library

// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------


// Constructors 
//--------------------------------------------------------------------
Component::Component() : m_p_parent(0), m_refCounter(0)
{
    m_name = "Component";
}

// Destructor
//--------------------------------------------------------------------
Component::~Component()
{

}

// Public methods
//--------------------------------------------------------------------
bool Component::addChild(Component *)
{
	return false;
}


bool Component::removeChild(Component *)
{
	return false;
}


void Component::ref()
{
	m_refCounter++;
}

void Component::unref()
{
	m_refCounter--;
	if(m_refCounter <= 0)
	{
        delete this;
	}
}

void Component::onParentSet(Component *)
{

}

void Component::setParent(Component * a_p_parent)
{
	m_p_parent = a_p_parent;
	onParentSet(m_p_parent);
}

void Component::setName(const QString & name)
{
    m_name = name;
    onNameChanged();
}

QString Component::name() const
{
    return m_name;
}

void Component::onNameChanged()
{

}

//--------------------------------------------------------------------
void Component::print(int a_depth /*= 0*/)
{
	for(int i = 0 ; i < a_depth - 1 ; i++)
	{
		std::cout << "  ";
	}

	if(m_p_parent != 0)
	{
		std::cout << "|-";
	}

	if(nbChildren() != 0)
	{
		std::cout << "+ ";
	}
	else
	{
		std::cout << " ";
	}

	doPrint();

	a_depth++;

	for(unsigned int i = 0 ; i < nbChildren() ; i++)
	{
		getChild(i)->print(a_depth);
	}
}

void Component::copy(const Component & component, bool /* recursive*/)
{
	m_name = component.m_name;
}

void Component::doPrint()
{
  std::cout << m_name.toStdString() << " (address : " << std::hex << this << ")" << std::endl;
}

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
