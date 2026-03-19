#pragma once

#include <list>

class Event;
class Observable;

#define _OBSERVER_API_

class _OBSERVER_API_ Observer
{
public:
	Observer();
	virtual ~Observer();

	virtual void onDestroyEvent()
	{
	}

	virtual void onEvent(Event*) = 0;

private:
	void treatEvent(Event*);
	void addObservable(Observable*);
	void removeObservable(Observable*);
	std::list<Observable*>::iterator findObservable(Observable*);
	std::list<Observable*> m_observableList;
	friend class Observable;
};
