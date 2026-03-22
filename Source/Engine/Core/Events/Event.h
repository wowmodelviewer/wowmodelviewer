#pragma once

class Observable;

/// @brief Lightweight event object carrying a type tag and sender reference.
///
/// Used by the Observer pattern to notify subscribers of state changes.
class Event
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
