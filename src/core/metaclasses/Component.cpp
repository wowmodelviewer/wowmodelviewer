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

// Qt 
#include "Logger.h"

// Externals

// Other libraries

// Current library

// Namespaces used
//--------------------------------------------------------------------

// Beginning of implementation
//--------------------------------------------------------------------


// Constructors 
//--------------------------------------------------------------------
Component::Component() : parent_(nullptr), refCounter_(0)
{
    name_ = "Component";
}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------
void Component::ref()
{
  refCounter_++;
}

void Component::unref()
{
  refCounter_--;
  if(refCounter_ <= 0)
  {
    delete this;
  }
}

void Component::setParentComponent(Component * parent)
{
  parent_ = parent;
  onParentSet(parent_);
}

void Component::setName(const QString & name)
{
    name_ = name;
    onNameChanged();
}

QString Component::name() const
{
    return name_;
}

//--------------------------------------------------------------------
void Component::print(int depth /*= 0*/)
{
  QString prefix;
  for(auto i = 0 ; i < depth - 1 ; i++)
  {
    prefix += "  ";
  }

  if(parent_ != nullptr)
  {
    prefix += "|-";
  }

  if(nbChildren() != 0)
  {
    prefix += "+ ";
  }
  else
  {
    prefix += " ";
  }

  doPrint(prefix);

  depth++;

  for(unsigned int i = 0 ; i < nbChildren() ; i++)
  {
    getChild(i)->print(depth);
  }
}

void Component::copy(const Component & component, bool /* recursive*/)
{
  name_ = component.name_;
}

void Component::doPrint(const QString& prefix)
{
  LOG_INFO << prefix << name_ << " (address : " << hex << this << ")";
}

// Protected methods
//--------------------------------------------------------------------

// Private methods
//--------------------------------------------------------------------
