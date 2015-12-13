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
 * BaseIterator.h
 *
 *  Created on: 5 mai 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _BASEITERATOR_H_
#define _BASEITERATOR_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <vector>

// Qt

// Externals

// Other libraries

// Current library

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
template <class ItemType>
class BaseIterator
{
  public :
    // Constants / Enums

    // Constructors
    BaseIterator();

    // Destructors
    virtual ~BaseIterator();

    // Methods
    void begin();

    bool end();

    void operator++(int);

    int size() { return m_items.size(); }

  protected :
    // Constants / Enums

    // Constructors


    // Destructors

    // Methods

    // Members
    std::vector<ItemType * > m_items;
    typename std::vector<ItemType * >::iterator m_internalIt;

  private :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods

    // Members

    // friend class declarations
};

// static members definition
#ifdef _BASEITERATOR_CPP_

#endif

// Constructors
//--------------------------------------------------------------------
template<class ItemType>
BaseIterator<ItemType>::BaseIterator()
{
  m_internalIt = m_items.begin();
}

// Destructor
//--------------------------------------------------------------------
template<class ItemType>
BaseIterator<ItemType>::~BaseIterator()
{

}
// Public methods
//--------------------------------------------------------------------
template<class ItemType>
void BaseIterator<ItemType>::begin()
{
  m_internalIt = m_items.begin();
}

template<class ItemType>
bool BaseIterator<ItemType>::end()
{
  return m_items.end();
}

template<class ItemType>
void BaseIterator<ItemType>::operator++(int)
{
  ++m_internalIt;
}

#endif /* _BASEITERATOR_H_ */
