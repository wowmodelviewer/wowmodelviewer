#pragma once

#include <list>
#include "Event.h"

class Observer;

#define _OBSERVABLE_API_

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
