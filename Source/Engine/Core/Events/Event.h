#pragma once

class Observable;

#define _EVENT_API_

/// @brief Lightweight event object carrying a type tag and sender reference.
///
/// Used by the Observer pattern to notify subscribers of state changes.
class _EVENT_API_ Event
{
public:
	enum EventType
	{
		DESTROYED = 0x00000000
	};

	Event(Observable*, EventType);

	virtual ~Event() = default;

	EventType type() const { return m_type; }
	void setType(EventType type) { m_type = type; }

	Observable* sender() { return m_p_sender; }

private:
	EventType m_type;
	Observable* m_p_sender;
};
