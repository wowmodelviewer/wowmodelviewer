#include "Observable.h"
#include "Event.h"
#include "Observer.h"

Observable::Observable()
{
	m_observerList.clear();
}

Observable::~Observable()
{
	// we send an event to observers attached in order to react to observable destruction
	Event l_event(this, Event::DESTROYED);
	notify(l_event);

	// we remove
	for (std::list<Observer*>::iterator l_it = m_observerList.begin(); l_it != m_observerList.end();)
	{
		(*l_it)->removeObservable(this);
		l_it = m_observerList.erase(l_it);
	}
}

void Observable::attach(Observer* observer)
{
	if (observerAttached(observer) == m_observerList.end())
	{
		m_observerList.push_back(observer);
		observer->addObservable(this);
	}
}

void Observable::detach(Observer* observer)
{
	std::list<Observer*>::iterator l_it = observerAttached(observer);

	if (l_it != m_observerList.end())
	{
		m_observerList.erase(l_it);
		observer->removeObservable(this);
	}
}

void Observable::notify(Event& event)
{
	std::list<Observer*>::iterator l_it;

	for (l_it = m_observerList.begin(); l_it != m_observerList.end(); l_it++)
	{
		(*l_it)->treatEvent(&event);
	}
}

std::list<Observer*>::iterator Observable::observerAttached(Observer* observer)
{
	std::list<Observer*>::iterator l_result = m_observerList.end();
	if (observer != nullptr)
	{
		std::list<Observer*>::iterator l_it;
		for (l_it = m_observerList.begin(); l_it != m_observerList.end(); l_it++)
		{
			if ((*l_it) == observer)
			{
				l_result = l_it;
				break;
			}
		}
	}
	return l_result;
}
