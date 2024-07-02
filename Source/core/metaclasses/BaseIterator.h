#pragma once

#include <vector>

template <class ItemType>
class BaseIterator
{
public:
	BaseIterator();
	virtual ~BaseIterator();

	void begin();

	bool end();

	void operator++(int);

	int size() { return m_items.size(); }

protected:
	std::vector<ItemType*> m_items;
	typename std::vector<ItemType*>::iterator m_internalIt;
};

template <class ItemType>
BaseIterator<ItemType>::BaseIterator()
{
	m_internalIt = m_items.begin();
}

template <class ItemType>
BaseIterator<ItemType>::~BaseIterator()
{
}

template <class ItemType>
void BaseIterator<ItemType>::begin()
{
	m_internalIt = m_items.begin();
}

template <class ItemType>
bool BaseIterator<ItemType>::end()
{
	return m_items.end();
}

template <class ItemType>
void BaseIterator<ItemType>::operator++(int)
{
	++m_internalIt;
}
