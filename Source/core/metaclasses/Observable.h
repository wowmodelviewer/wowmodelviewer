#pragma once

#include <list>
#include "Event.h"

class Observer;

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _OBSERVABLE_API_ __declspec(dllexport)
#    else
#        define _OBSERVABLE_API_ __declspec(dllimport)
#    endif
#else
#    define _OBSERVABLE_API_
#endif

class _OBSERVABLE_API_ Observable
{
public:
	Observable();
	virtual ~Observable();

	void attach(Observer*);
	void detach(Observer*);

protected:
	void notify(Event&);

private:
	std::list<Observer*>::iterator observerAttached(Observer*);
	std::list<Observer*> m_observerList;
};
