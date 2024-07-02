#pragma once

#include <unordered_set>
#include "Component.h"

template <class DataType>
class Container : public Component
{
public:
	typedef typename std::unordered_set<DataType*>::iterator iterator;

	Container();
	virtual ~Container();

	virtual bool addChild(DataType* child);
	virtual bool removeChild(DataType* child);
	void removeAllChildren();

	virtual void onChildAdded(DataType*)
	{
	};

	virtual void onChildRemoved(DataType*)
	{
	};

	template <class ChildType>
	int removeAllChildrenOfType();

	unsigned int nbChildren() const { return static_cast<unsigned int>(m_children.size()); }

	bool findChildComponent(Component* child, bool recursive = false);
	Component* getChild(unsigned int index);
	const Component* getChild(unsigned int index) const;

	iterator begin()
	{
		return m_children.begin();
	}

	iterator end()
	{
		return m_children.end();
	}

private:
	std::unordered_set<DataType*> m_children;
};

template <class DataType>
Container<DataType>::Container()
{
	m_children.clear();
	setName("Container");
}

template <class DataType>
Container<DataType>::~Container()
{
	typename std::unordered_set<DataType*>::iterator l_it;
	for (l_it = m_children.begin(); l_it != m_children.end(); l_it++)
	{
		(*l_it)->unref();
	}
}

template <class DataType>
bool Container<DataType>::addChild(DataType* child)
{
	m_children.insert(child);

	child->setParentComponent(this);

	// for hierarchy notification
	onChildAdded(child);

	return true;
}

template <class DataType>
bool Container<DataType>::removeChild(DataType* child)
{
	// ok, we remove this child from the list
	m_children.erase(child);
	child->setParentComponent(0);

	// for hierarchy notification
	onChildRemoved(child);

	child->unref();
	return true;
}

template <class DataType>
void Container<DataType>::removeAllChildren()
{
	while (!m_children.empty())
	{
		removeChild(*m_children.begin());
	}
}

template <class DataType>
template <class ChildType>
int Container<DataType>::removeAllChildrenOfType()
{
	typename std::list<DataType*>::iterator l_it;
	std::list<DataType*> l_childrenToRemove;
	for (l_it = m_children.begin(); l_it != m_children.end(); l_it++)
	{
		if (dynamic_cast<ChildType*>(*l_it) != nullptr)
		{
			l_childrenToRemove.push_back(*l_it);
		}
	}

	int l_result = l_childrenToRemove.size();

	for (l_it = l_childrenToRemove.begin(); l_it != l_childrenToRemove.end(); l_it++)
	{
		removeChild(*l_it);
	}

	return l_result;
}

template <class DataType>
bool Container<DataType>::findChildComponent(Component* child, bool recursive /* = false */)
{
	auto l_it = m_children.find(dynamic_cast<DataType*>(child));

	if (l_it != m_children.end())
		return true;

	// resursive part
	if (recursive)
	{
		auto l_itEnd = m_children.end();
		for (l_it = m_children.begin(); l_it != l_itEnd; ++l_it)
		{
			if ((*l_it)->findChildComponent(child, recursive))
				return true;
		}
	}

	return false;
}

template <class DataType>
Component* Container<DataType>::getChild(unsigned int index)
{
	DataType* l_p_result = nullptr;
	if (index < m_children.size())
	{
		unsigned int l_index = 0;
		typename std::unordered_set<DataType*>::iterator l_it;
		for (l_it = m_children.begin(); l_index < index; l_index++)
		{
			l_it++;
		}
		l_p_result = *l_it;
	}
	return l_p_result;
}

template <class DataType>
const Component* Container<DataType>::getChild(unsigned int index) const
{
	const DataType* l_p_result = nullptr;
	if (index < m_children.size())
	{
		unsigned int l_index = 0;
		typename std::unordered_set<DataType*>::const_iterator l_it;
		for (l_it = m_children.begin(); l_index < index; l_index++)
		{
			l_it++;
		}
		l_p_result = *l_it;
	}
	return l_p_result;
}
