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
    virtual bool addChild(DataType * child);
    virtual bool removeChild(DataType * child);
    virtual void removeAllChildren();

    virtual void onChildAdded(DataType *) {}
    virtual void onChildRemoved(DataType *) {}

    template <class ChildType>
      int removeAllChildrenOfType();

    unsigned int nbChildren() const override {return children_.size(); }

    bool findChildComponent(Component * child, bool recursive = false) override;
    Component * getChild(unsigned int index) override;
    const Component * getChild(unsigned int index) const override;
    
    iterator begin()
    {
      return children_.begin();
    }

    iterator end()
    {
      return children_.end();
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
    std::unordered_set<DataType *> children_;

    // friend class declarations
};

template<class DataType>
Container<DataType>::Container()
{
  children_.clear();
  setName("Container");
}

template<class DataType>
Container<DataType>::~Container()
{
  for(auto & it : children_)
    it->unref();
}

template<class DataType>
bool Container<DataType>::addChild(DataType * child)
{
  if (!child)
    return false;

  children_.insert(child);

  child->setParentComponent(this);

  // for hierarchy notification
  onChildAdded(child);

  return true;
}

template<class DataType>
bool Container<DataType>::removeChild(DataType * child)
{
  if (!child)
    return false;

  // ok, we remove this child from the list
  children_.erase(child);
  child->setParentComponent(0);

  // for hierarchy notification
  onChildRemoved(child);

  child->unref();
  return true;
}

template<class DataType>
void Container<DataType>::removeAllChildren()
{
  while(!children_.empty())
    removeChild(*children_.begin());
}

template <class DataType> template <class ChildType>
int Container<DataType>::removeAllChildrenOfType()
{
  std::list<DataType *> childrenToRemove;

  for(auto & it : children_)
  {
    if(dynamic_cast<ChildType *>(*it) != nullptr)
    {
      childrenToRemove.push_back(*it);
    }
  }

  for(auto & it : childrenToRemove)
    removeChild(it);

  return childrenToRemove.size();
}



template<class DataType>
bool Container<DataType>::findChildComponent(Component * child, bool recursive /* = false */)
{
  if(children_.find(dynamic_cast<DataType *>(child)) != children_.end())
    return true;

  // resursive part
  if(recursive)
  {
    for(auto & it : children_)
    {
      if(it->findChildComponent(child,recursive))
        return true;
    }
  }

  return false;
}

template<class DataType>
Component * Container<DataType>::getChild(unsigned int index)
{
  DataType * result = nullptr;
  if(index < children_.size())
  {
    unsigned int idx = 0;
    typename std::unordered_set<DataType *>::iterator it;
    for(it = children_.begin() ; idx < index ;  idx++)
      ++it;

    result = *it;
  }
  return result;
}

template<class DataType>
const Component * Container<DataType>::getChild(unsigned int index) const
{
  const DataType * result = nullptr;
  if(index < children_.size())
  {
    unsigned int idx = 0;
    typename std::unordered_set<DataType *>::const_iterator it;
    for(it = children_.begin() ; idx < index ; idx++)
      ++it;

    result = *it;
  }
  return result;
}

#endif /* _CONTAINER_H_ */
