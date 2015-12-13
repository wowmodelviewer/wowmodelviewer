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
 * Component.h
 *
 *  Created on: 23 dec 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */
 
#ifndef _COMPONENT_H_
#define _COMPONENT_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <iostream>
#include <string.h>
#include <typeinfo> // for std::bad_cast in inherited classes

// Qt 
#include <Qstring>

// Externals

// Other libraries

// Current library

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _COMPONENT_API_ __declspec(dllexport)
#    else
#        define _COMPONENT_API_ __declspec(dllimport)
#    endif
#else
#    define _COMPONENT_API_
#endif

class _COMPONENT_API_ Component
{
	public :
		// Constants / Enums

		// Constructors 
		Component();

		// Destructors
		virtual ~Component();

		// Methods
		// children management
		virtual bool addChild(Component *);
		virtual bool removeChild(Component *);
		virtual void removeAllChildren() { }

		virtual void onChildAdded(Component *);
		virtual void onChildRemoved(Component *);

		virtual unsigned int nbChildren() const {return 0; }

		virtual Component * findChild(Component * /* component */,bool /* recursive */ ) { return 0; }
		virtual Component * getChild(unsigned int /* index */) { return 0; }
		virtual const Component * getChild(unsigned int /* index */) const { return 0; }

		// parent management
		void setParent(Component *);
		virtual void onParentSet(Component *);
		const Component * parent() const { return m_p_parent; }
		Component * parent() { return m_p_parent; }

		template <class DataType>
				   const DataType * firstParentOfType();

		// auto delete management
		void ref();
		void unref();

		// Name management
		void setName(const QString & name);
		QString name() const;
		virtual void onNameChanged();

		// misc
		void print(int l_depth = 0);
		// overlaod in inheritted classes to perform specific stuff at display time
		virtual void doPrint();

		// copy
		void copy(const Component & component, bool /* recursive*/);

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
		Component * m_p_parent;

		unsigned int m_refCounter;

		QString m_name;

		// friend class declarations
};

template <class DataType>
const DataType * Component::firstParentOfType()
{
	if(parent() != 0)
	{
		DataType * l_p_parent = dynamic_cast<DataType *>(parent());
		if(l_p_parent != 0)
			return l_p_parent;
		else
			return parent()->firstParentOfType<DataType>();
	}
	return 0;
}

// static members definition
#ifdef _COMPONENT_CPP_

#endif

#endif /* _COMPONENT_H_ */
