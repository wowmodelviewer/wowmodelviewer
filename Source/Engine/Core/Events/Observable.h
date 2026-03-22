#pragma once

#include <list>
#include "Event.h"

class Observer;

#define _OBSERVABLE_API_

/// @brief Subject in the Observer pattern; maintains a list of Observer subscribers.
///
/// Derived classes call notify() to broadcast events to all attached observers.
class _OBSERVABLE_API_ Observable
{
public:
	Observable();
	virtual ~Observable();

	/// @brief Subscribe an observer to receive events from this object.
	void attach(Observer*);

	/// @brief Unsubscribe an observer.
	void detach(Observer*);

protected:
	/// @brief Broadcast an event to all attached observers.
	void notify(Event&);

private:
	std::list<Observer*>::iterator observerAttached(Observer*);
	std::list<Observer*> m_observerList;
};
