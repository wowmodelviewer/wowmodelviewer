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
 * ConstIterator.h
 *
 *  Created on: 26 may 2012
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */
 
#ifndef _CONSTITERATOR_H_
#define _CONSTITERATOR_H_

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
class ConstIterator : public BaseIterator<ItemType>
{
	public :
		// Constants / Enums

		// Constructors 
		ConstIterator(const Component *);
		ConstIterator(const Component &);

		// Destructors
		virtual ~ConstIterator();

		// Methods

		const ItemType * operator *();

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
		void buildList(const Component & component)
		{
			this->m_items.clear();
			for(unsigned int i = 0 ; i < component.nbChildren() ; i++ )
			{
				const ItemType * l_p_curChild = dynamic_cast<const ItemType *>(component.getChild(i));
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
#ifdef _CONSTITERATOR_CPP_

#endif

// Constructors
//--------------------------------------------------------------------
template<class ItemType>
ConstIterator<ItemType>::ConstIterator(const Component * component)
	: BaseIterator<ItemType>()
{
	if(component)
		buildList(*component);
}

template<class ItemType>
ConstIterator<ItemType>::ConstIterator(const Component & component)
  : BaseIterator<ItemType>()
{
    buildList(component);
}

// Destructor
//--------------------------------------------------------------------
template<class ItemType>
ConstIterator<ItemType>::~ConstIterator()
{

}

// Public methods
//--------------------------------------------------------------------
template<class ItemType>
const ItemType * ConstIterator<ItemType>::operator *()
{
	if(!this->ended())
	{
		return this->m_items[this->m_index];
	}
	return 0;
}

// Private methods
//--------------------------------------------------------------------

#endif /* _CONSTITERATOR_H_ */
