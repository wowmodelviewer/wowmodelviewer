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
 * Iterator.h
 *
 *  Created on: 4 may 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */
 
#ifndef _ITERATOR_H_
#define _ITERATOR_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <vector>

// Qt 

// Externals

// Other libraries

// Current library
#include "BaseIterator.h"
#include "Component.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
template <class ItemType>
class Iterator : public BaseIterator<ItemType>
{
	public :
		// Constants / Enums

		// Constructors 
		Iterator(Component *);
		Iterator(Component &);

		// Destructors
		virtual ~Iterator();

		// Methods
		ItemType * operator *();

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
		void buildList(Component & component)
		{
			this->m_items.clear();
			for(unsigned int i = 0 ; i < component.nbChildren() ; i++ )
			{
				ItemType * l_p_curChild = dynamic_cast<ItemType *>(component.getChild(i));
				if(l_p_curChild != 0)
				{
					this->m_items.push_back(l_p_curChild);
				}
			}
		}

		// Members

		// friend class declarations
};

// static members definition
#ifdef _ITERATOR_CPP_

#endif

// Constructors
//--------------------------------------------------------------------
template<class ItemType>
Iterator<ItemType>::Iterator(Component * component)
	: BaseIterator<ItemType>()
{
	if(component)
		buildList(*component);
}

template<class ItemType>
Iterator<ItemType>::Iterator(Component & component)
  : BaseIterator<ItemType>()
{
    buildList(component);
}


// Destructor
//--------------------------------------------------------------------
template<class ItemType>
Iterator<ItemType>::~Iterator()
{

}
// Public methods
//--------------------------------------------------------------------
template<class ItemType>
ItemType * Iterator<ItemType>::operator *()
{
	if(!this->ended())
	{
		return this->m_items[this->m_index];
	}
	return 0;
}

#endif /* _ITERATOR_H_ */
