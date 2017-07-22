#ifndef MANAGER_H
#define MANAGER_H

// STL
#include <iostream>
#include <map>

#include <QString>

#include "GameFile.h"
#include "OpenGLHeaders.h"

#include "logger/Logger.h"

// base class for manager objects

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _MANAGEDITEM_API_ __declspec(dllexport)
#    else
#        define _MANAGEDITEM_API_ __declspec(dllimport)
#    endif
#else
#    define _MANAGEDITEM_API_
#endif

class _MANAGEDITEM_API_ ManagedItem
{
	int m_refcount;
	QString m_itemName;
public:
  ManagedItem(QString n): m_refcount(0), m_itemName(n) {}
	virtual ~ManagedItem() {}

	void addref()
	{
    ++m_refcount;
	}

	bool delref()
	{
    return --m_refcount == 0;
	}
	
	void setItemName(QString name) { m_itemName = name; }
  QString itemName() { return m_itemName; }
  int refCount() { return m_refcount; }
};



template <class IDTYPE>
class Manager {
public:
	std::map<QString, IDTYPE> names;
	std::map<IDTYPE, ManagedItem*> items;

	Manager()
	{
	}

	~Manager()
	{
	  names.clear();
	  items.clear();
	}

	virtual IDTYPE add(GameFile *) = 0;

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

	void delbyname(QString name)
	{
		if (has(name)) 
			del(get(name));
	}

	virtual void doDelete(IDTYPE) {}

	bool has(QString name)
	{
		return (names.find(name) != names.end());
	}

	IDTYPE get(QString name)
	{
	  std::map<QString, IDTYPE>::iterator it = names.find(name);
	  if(it != names.end())
	    return it->second;

		return 0;
	}

	QString get(IDTYPE id)
	{
    QString result = "";
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
		for (size_t i=0; i<50; i++) {
			if(items.find((const unsigned int)i) != items.end()) {
				del((GLuint)i);
			}
		}
		
		names.clear();
		items.clear();
	}

  void dump()
  {
    for (auto it : items)
      LOG_INFO << it.second->itemName() << it.second->refCount();
  }

protected:
	void do_add(QString name, IDTYPE id, ManagedItem* item)
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

