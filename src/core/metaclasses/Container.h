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
 * Container.h
 *
 *  Created on: 23 dec 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */
 
#ifndef _CONTAINER_H_
#define _CONTAINER_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <iostream>
#include <unordered_set>

// Qt 

// Externals

// Other libraries

// Current library
#include "Component.h"

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
template <class DataType>
class Container : public Component
{
	public :

    typedef typename std::unordered_set<DataType *>::iterator iterator;

		// Constructors 
		Container();

		// Destructors
		virtual ~Container();

		// Methods
		bool addChild(DataType * child);
		bool removeChild(DataType * child);
		void removeAllChildren();

		template <class ChildType>
			int removeAllChildrenOfType();

		unsigned int nbChildren() const {return (unsigned int)m_children.size(); }

		bool findChild(Component * child, bool recursive = false);
		Component * getChild(unsigned int index);
		const Component * getChild(unsigned int index) const;
		
		iterator begin()
		{
		  return m_children.begin();
		}

		iterator end()
		{
		  return m_children.end();
		}

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
		std::unordered_set<DataType *> m_children;

		// friend class declarations
};

template<class DataType>
Container<DataType>::Container()
{
	m_children.clear();
	setName("Container");
}

template<class DataType>
Container<DataType>::~Container()
{
	typename std::unordered_set<DataType *>::iterator l_it;
	for(l_it = m_children.begin() ; l_it != m_children.end() ; l_it++)
	{
		(*l_it)->unref();
	}
}

template<class DataType>
bool Container<DataType>::addChild(DataType * child)
{
	m_children.insert(child);

	child->setParent(this);

	// for hierarchy notification
	onChildAdded(child);

	return true;
}

template<class DataType>
bool Container<DataType>::removeChild(DataType * child)
{
	// ok, we remove this child from the list
	m_children.erase(child);
	child->setParent(0);

	// for hierarchy notification
	onChildRemoved(child);

	child->unref();
	return true;
}

template<class DataType>
void Container<DataType>::removeAllChildren()
{
	while(!m_children.empty())
	{
		removeChild(*m_children.rbegin());
	}
}

template <class DataType> template <class ChildType>
int Container<DataType>::removeAllChildrenOfType()
{
	typename std::list<DataType *>::iterator l_it;
	std::list<DataType *> l_childrenToRemove;
	for(l_it = m_children.begin() ; l_it != m_children.end() ; l_it++)
	{
		if(dynamic_cast<ChildType *>(*l_it) != 0)
		{
			l_childrenToRemove.push_back(*l_it);
		}
	}

	int l_result = l_childrenToRemove.size();

	for(l_it = l_childrenToRemove.begin() ; l_it != l_childrenToRemove.end() ; l_it++)
	{
		removeChild(*l_it);
	}

	return l_result;
}



template<class DataType>
bool Container<DataType>::findChild(Component * child, bool recursive /* = false */)
{
	std::unordered_set<DataType *>::const_iterator l_it = m_children.find(dynamic_cast<DataType *>(child));

	if(l_it != m_children.end())
	  return true;

	// resursive part
	if(recursive)
	{
	  std::unordered_set<DataType *>::const_iterator l_itEnd = m_children.end();
	  for(l_it = m_children.begin() ; l_it != l_itEnd ; ++l_it)
	  {
	    if((*l_it)->findChild(child,recursive))
	      return true;
	  }
	}

	return 0;
}

template<class DataType>
Component * Container<DataType>::getChild(unsigned int index)
{
	DataType * l_p_result = 0;
	if(index < m_children.size())
	{
	  unsigned int l_index = 0;
		typename std::unordered_set<DataType *>::iterator l_it;
    for(l_it = m_children.begin() ; l_index < index ;  l_index++)
		{
    	l_it++;
		}
		l_p_result = *l_it;
	}
	return l_p_result;
}

template<class DataType>
const Component * Container<DataType>::getChild(unsigned int index) const
{
	const DataType * l_p_result = 0;
	if(index < m_children.size())
	{
	  unsigned int l_index = 0;
		typename std::unordered_set<DataType *>::const_iterator l_it;
        for(l_it = m_children.begin() ; l_index < index ; l_index++)
		{
             l_it++;
		}
		l_p_result = *l_it;
	}
	return l_p_result;
}

#endif /* _CONTAINER_H_ */
