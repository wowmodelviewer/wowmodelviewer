#include "Observer.h"
#include "../core/metaclasses/Event.h"
#include "../core/metaclasses/Observable.h"

Observer::Observer()
{
	m_observableList.clear();
}

Observer::~Observer()
{
	std::list<Observable*>::iterator l_it;
	while (!m_observableList.empty())
	{
		l_it = m_observableList.begin();
		(*l_it)->detach(this);
	}
}

void Observer::treatEvent(Event* event)
{
	if (!event)
		return;

	switch (event->type())
	{
	case Event::DESTROYED:
		onDestroyEvent();
		break;

	default:
		onEvent(event);
		break;
	}
}

void Observer::addObservable(Observable* obs)
{
	std::list<Observable*>::iterator l_toRemove = findObservable(obs);
	if (l_toRemove == m_observableList.end())
	{
		m_observableList.push_back(obs);
	}
}

void Observer::removeObservable(Observable* obs)
{
	m_observableList.erase(findObservable(obs));
}

std::list<Observable*>::iterator Observer::findObservable(Observable* obs)
{
	std::list<Observable*>::iterator l_result = m_observableList.end();

	if (obs != nullptr)
	{
		std::list<Observable*>::iterator l_it;
		for (l_it = m_observableList.begin(); l_it != m_observableList.end(); l_it++)
		{
			if (*l_it == obs)
			{
				l_result = l_it;
				break;
			}
		}
	}

	return l_result;
}
