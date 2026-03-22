#pragma once

#include <map>
#include <string>
#include <glad/gl.h>
// ReSharper disable once CppUnusedIncludeDirective
#include "GameFile.h"
#include "Logger.h"

#define _MANAGEDITEM_API_

/// @brief Reference-counted item stored in a Manager.
///
/// Tracks its own name and reference count; the owning Manager deletes
/// the item when its count drops to zero.
class _MANAGEDITEM_API_ ManagedItem
{
	int m_refcount;
	std::string m_itemName;

public:
	ManagedItem(std::string n): m_refcount(0), m_itemName(std::move(n))
	{
	}

	virtual ~ManagedItem()
	{
	}

	void addref()
	{
		++m_refcount;
	}

	bool delref()
	{
		return --m_refcount == 0;
	}

	void setItemName(std::string name) { m_itemName = std::move(name); }
	const std::string& itemName() const { return m_itemName; }
	int refCount() { return m_refcount; }
};

/// @brief Generic name-to-ID resource manager with reference-counted items.
/// @tparam IDTYPE  The numeric ID type used to identify managed items (e.g. GLuint).
template <class IDTYPE>
class Manager
{
public:
	std::map<std::string, IDTYPE> names;
	std::map<IDTYPE, ManagedItem*> items;

	Manager() = default;

	~Manager()
	{
		names.clear();
		items.clear();
	}

	virtual IDTYPE add(GameFile*) = 0;

	virtual void del(IDTYPE id)
	{
		if (items.find(id) == items.end())
		{
			doDelete(id);
			return; // if we can't find the item id, delete the texture
		}

		if (items[id]->delref())
		{
			ManagedItem* i = items[id];

			if (!i)
				return;

			doDelete(id);

			auto it = names.find(i->itemName());
			if (it != names.end()) // Ensure the iterator is valid before erasing
			{
				names.erase(it);
			}

			items.erase(items.find(id));
			delete i;
		}
	}

	void delbyname(const std::string& name)
	{
		if (has(name))
			del(get(name));
	}

	virtual void doDelete(IDTYPE)
	{
	}

	bool has(const std::string& name)
	{
		return (names.find(name) != names.end());
	}

	IDTYPE get(const std::string& name)
	{
		auto it = names.find(name);
		if (it != names.end())
			return it->second;

		return 0;
	}

	std::string get(IDTYPE id)
	{
		std::string result;
		for (auto const& it : names)
		{
			if (it.second == id)
			{
				result = it.first;
				break;
			}
		}
		return result;
	}

	void clear()
	{
		for (size_t i = 0; i < 50; i++)
		{
			if (items.find(static_cast<const unsigned int>(i)) != items.end())
			{
				del((GLuint)i);
			}
		}

		names.clear();
		items.clear();
	}

	void dump()
	{
		for (auto it : items)
			LOG_INFO << it.first << "->" << it.second->itemName() << "(" << it.second->refCount() << ")";
	}

protected:
	void do_add(const std::string& name, IDTYPE id, ManagedItem* item)
	{
		names[name] = id;
		item->addref();
		items[id] = item;
	}
};

class SimpleManager : public Manager<int>
{
	int baseid;

public:
	SimpleManager() : baseid(0)
	{
	}

protected:
	int nextID()
	{
		return baseid++;
	}
};
