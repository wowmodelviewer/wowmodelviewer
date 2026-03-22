#pragma once

#include <list>

class Event;
class Observable;

#define _OBSERVER_API_

/// @brief Listener in the Observer pattern; receives events from Observable subjects.
///
/// Subclasses must implement onEvent() to handle incoming notifications.
class _OBSERVER_API_ Observer
{
public:
	Observer();
	virtual ~Observer();

	/// @brief Called when an observed subject is destroyed.
	virtual void onDestroyEvent()
	{
	}

	/// @brief Handle an incoming event. Must be implemented by subclasses.
	virtual void onEvent(Event*) = 0;

private:
	void treatEvent(Event*);
	void addObservable(Observable*);
	void removeObservable(Observable*);
	std::list<Observable*>::iterator findObservable(Observable*);
	std::list<Observable*> m_observableList;
	friend class Observable;
};
