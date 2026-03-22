#pragma once

#include <vector>

/// @brief Simple forward iterator over a collection of pointers.
/// @tparam ItemType The pointed-to element type.
template <class ItemType>
class BaseIterator
{
public:
	BaseIterator();
	virtual ~BaseIterator();

	/// @brief Reset the iterator to the first element.
	void begin();

	/// @brief Check whether the iterator has reached the end.
	bool end();

	/// @brief Advance the iterator (postfix).
	void operator++(int);

	/// @brief Return the number of items.
	int size() { return m_items.size(); }

protected:
	std::vector<ItemType*> m_items;  ///< Stored item pointers.
	typename std::vector<ItemType*>::iterator m_internalIt;  ///< Current position.
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
