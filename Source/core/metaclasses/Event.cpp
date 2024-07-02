#include "Event.h"
#include "Observable.h"

Event::Event(Observable* observable, EventType type) : m_type(type), m_p_sender(observable)
{
}
