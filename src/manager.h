#ifndef MANAGER_H
#define MANAGER_H

#if defined(_WINDOWS) && !defined(_MINGW)
    #pragma warning( disable : 4100 )
#endif

// wxWidgets
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// STL
#include <string>
#include <map>

#include "OpenGLHeaders.h"

// base class for manager objects

class ManagedItem {
	int refcount;
public:
	wxString name;
	ManagedItem(wxString n): refcount(0), name(n) { }
	virtual ~ManagedItem() {}

	void addref()
	{
		++refcount;
	}

	bool delref()
	{
		return --refcount==0;
	}
	
};



template <class IDTYPE>
class Manager {
public:
	std::map<wxString, IDTYPE> names;
	std::map<IDTYPE, ManagedItem*> items;

	Manager()
	{
	}

	virtual IDTYPE add(wxString name) = 0;

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

			#ifdef _DEBUG
				wxLogMessage(wxT("Unloading Texture: %s"), i->name.c_str());
			#endif

			doDelete(id);
			names.erase(names.find(i->name));
			items.erase(items.find(id));

			wxDELETE(i);
		}
	}

	void delbyname(wxString name)
	{
		if (has(name)) 
			del(get(name));
	}

	virtual void doDelete(IDTYPE) {}

	bool has(wxString name)
	{
		return (names.find(name) != names.end());
	}

	IDTYPE get(wxString name)
	{
		return names[name];
	}

	wxString get(IDTYPE id)
	{
		return names[id];
	}

	void clear()
	{
		/*
		for (std::map<IDTYPE, ManagedItem*>::iterator it=items.begin(); it!=items.end(); ++it) {
			ManagedItem *i = (*it);
			
			wxDELETE(i);
		}
		*/

		for (size_t i=0; i<50; i++) {
			if(items.find((const unsigned int)i) != items.end()) {
				del((GLuint)i);
			}
		}
		
		names.clear();
		items.clear();
	}

protected:
	void do_add(wxString name, IDTYPE id, ManagedItem* item)
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

