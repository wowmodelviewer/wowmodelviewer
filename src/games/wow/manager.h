#ifndef MANAGER_H
#define MANAGER_H

// STL
#include <string>
#include <map>

#include <iostream>
#include "OpenGLHeaders.h"

// base class for manager objects

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _MANAGEDITEM_API_ __declspec(dllexport)
#    else
#        define _MANAGEDITEM_API_ __declspec(dllimport)
#    endif
#else
#    define _DISPLAYABLE_API_
#endif

class _MANAGEDITEM_API_ ManagedItem {
	int refcount;
	std::string m_itemName;
public:
	ManagedItem(std::string n): refcount(0), m_itemName(n) { }
	virtual ~ManagedItem() {}

	void addref()
	{
		++refcount;
	}

	bool delref()
	{
		return --refcount==0;
	}
	
	std::string itemName() { return m_itemName; }
};



template <class IDTYPE>
class Manager {
public:
	std::map<std::string, IDTYPE> names;
	std::map<IDTYPE, ManagedItem*> items;

	Manager()
	{
	}

	virtual IDTYPE add(std::string name) = 0;

	virtual void del(IDTYPE id)
	{
		if (items.find(id) == items.end())  {
			doDelete(id);
			return; // if we can't find the item id, delete the texture
		}

		if (items[id]->delref()) {
			ManagedItem *i = items[id];

			if (!i)
				return;

			doDelete(id);
			names.erase(names.find(i->itemName()));
			items.erase(items.find(id));

			delete i;
		}
	}

	void delbyname(std::string name)
	{
		if (has(name)) 
			del(get(name));
	}

	virtual void doDelete(IDTYPE) {}

	bool has(std::string name)
	{
		return (names.find(name) != names.end());
	}

	IDTYPE get(std::string name)
	{
		return names[name];
	}

	std::string get(IDTYPE id)
	{
	  return "";
		//return names[id];
	}

	void clear()
	{
		for (size_t i=0; i<50; i++) {
			if(items.find((const unsigned int)i) != items.end()) {
				del((GLuint)i);
			}
		}
		
		names.clear();
		items.clear();
	}

protected:
	void do_add(std::string name, IDTYPE id, ManagedItem* item)
	{
		names[name] = id;
		item->addref();
		items[id] = item;
	}
};

class SimpleManager : public Manager<int> {
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

#endif

