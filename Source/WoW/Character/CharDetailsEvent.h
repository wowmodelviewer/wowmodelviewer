/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * Event.h
 *
 *  Created on: 4 january 2015
 *   Copyright: 2015 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#pragma once

#include "Event.h"

/// @brief Event fired when character detail customisation options change.
class CharDetailsEvent : public Event
{
public:
	/// @brief Event types specific to character detail changes.
	enum EventType
	{
		CHOICE_LIST_CHANGED = 0x10000001  ///< A customisation choice list was modified.
	};

	/// @brief Construct a CharDetailsEvent.
	/// @param obs  The observable that fired the event.
	/// @param type The event type.
	CharDetailsEvent(Observable* obs, EventType type) : Event(obs, static_cast<Event::EventType>(type)),
														customizationOptionId_(0)
	{
	}

	/// @brief Set the customisation option ID associated with this event.
	void setCustomizationOptionId(const uint id) { customizationOptionId_ = id; }
	/// @brief Get the customisation option ID associated with this event.
	uint getCustomizationOptionId() const { return customizationOptionId_; }

private:
	uint customizationOptionId_;
};
