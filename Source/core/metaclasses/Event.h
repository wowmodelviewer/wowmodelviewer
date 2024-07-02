#pragma once

class Observable;

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _EVENT_API_ __declspec(dllexport)
#    else
#        define _EVENT_API_ __declspec(dllimport)
#    endif
#else
#    define _EVENT_API_
#endif

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
