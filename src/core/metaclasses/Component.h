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

// Qt 
#include <QString>

// Externals

// Other libraries

// Current library

// Namespaces used
//--------------------------------------------------------------------


// Class Declaration
//--------------------------------------------------------------------
class Component
{
  public :
    // Constants / Enums

    // Constructors 
    Component();

    // Destructors
    virtual ~Component() = default;

    // Methods
    // children management
    virtual bool addChild(Component *) { return false; }
    virtual bool removeChild(Component *) { return false; }
    virtual void removeAllChildren() {}

    virtual unsigned int nbChildren() const {return 0; }

    virtual bool findChildComponent(Component * /* component */, bool /* recursive */ ) { return false; }
    virtual Component * getChild(unsigned int /* index */) { return nullptr; }
    virtual const Component * getChild(unsigned int /* index */) const { return nullptr; }

    // parent management
    void setParentComponent(Component * parent);
    virtual void onParentSet(Component * /*parent*/) {}
    const Component * parent() const { return parent_; }
    Component * parent() { return parent_; }

    template <class DataType>
      const DataType * firstParentOfType();

    // auto delete management
    void ref();
    void unref();

    // Name management
    void setName(const QString & name);
    QString name() const;
    virtual void onNameChanged() {}

    // misc
    void print(int depth = 0);
    // overlaod in inheritted classes to perform specific stuff at display time
    virtual void doPrint(const QString & prefix = "");

    // copy
    void copy(const Component & component, bool recursive);

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
    Component * parent_;

    unsigned int refCounter_;

    QString name_;

    // friend class declarations
};

template <class DataType>
const DataType * Component::firstParentOfType()
{
  if(parent() != nullptr)
  {
    DataType * l_p_parent = dynamic_cast<DataType *>(parent());
    if(l_p_parent != nullptr)
      return l_p_parent;
    
    return parent()->firstParentOfType<DataType>();
  }
  return nullptr;
}

// static members definition
#ifdef _COMPONENT_CPP_

#endif

#endif /* _COMPONENT_H_ */
