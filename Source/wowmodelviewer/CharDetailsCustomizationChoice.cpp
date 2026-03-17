#include "CharDetailsCustomizationChoice.h"
#include <format>
#include "CharDetails.h"
#include "CharDetailsEvent.h"
#include "Game.h"
#include "logger/Logger.h"

IMPLEMENT_CLASS(CharDetailsCustomizationChoice, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsCustomizationChoice, wxWindow)
	EVT_CHOICE(wxID_ANY, CharDetailsCustomizationChoice::onChoice)
END_EVENT_TABLE()

CharDetailsCustomizationChoice::CharDetailsCustomizationChoice(wxWindow* parent, CharDetails& details, uint chrCustomizationChoiceID)
	: wxWindow(parent, wxID_ANY), ID_(chrCustomizationChoiceID), details_(details)
{
	const auto top = new wxFlexGridSizer(2, 0, 5);
	top->AddGrowableCol(1);

	details_.attach(this);

	const auto option = GAMEDATABASE.sqlQuery(std::format("SELECT Name_Lang FROM ChrCustomizationOption WHERE ID = {}", ID_));

	if (option.valid && !option.values.empty())
	{
		top->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(option.values[0][0])),
		         wxSizerFlags().Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

		choice_ = new wxChoice(this, wxID_ANY);

		top->Add(choice_, wxSizerFlags().Align(wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

		SetAutoLayout(true);
		top->SetSizeHints(this);
		SetSizer(top);

		buildList();
	}

	refresh();
}

void CharDetailsCustomizationChoice::onChoice(wxCommandEvent& event)
{
	LOG_INFO << __FUNCTION__ << event.GetSelection();
	details_.set(ID_, values_[event.GetSelection()]);
}

void CharDetailsCustomizationChoice::onEvent(Event* e)
{
	const auto* event = dynamic_cast<CharDetailsEvent*>(e);
	if (event && (event->type() == static_cast<Event::EventType>(CharDetailsEvent::CHOICE_LIST_CHANGED)) && (event->getCustomizationOptionId() == ID_))
	{
		const auto it = std::find(values_.begin(), values_.end(), details_.get(ID_));
		if (it != values_.end())
			choice_->SetSelection(it - values_.begin());
	}
}

void CharDetailsCustomizationChoice::buildList()
{
	if (choice_)
	{
		// clear list and re add possible choices
		choice_->Clear();
		values_.clear();

		const auto ids = details_.getCustomizationChoices(ID_);

		LOG_INFO << __FUNCTION__ << ID_;

		if (ids.empty())
			return;

		auto query = std::string("SELECT OrderIndex,Name_Lang,ID FROM ChrCustomizationChoice WHERE ID IN (");
		for (const auto id : ids)
		{
			query += std::to_string(id);
			query += ",";
		}

		query.pop_back();
		query += ") ORDER BY OrderIndex";

		LOG_INFO << query.c_str();

		const auto choices = GAMEDATABASE.sqlQuery(query);

		if (choices.valid && !choices.values.empty())
		{
			for (auto v : choices.values)
			{
				if (!v[1].empty())
					choice_->Append(wxString::FromUTF8(v[1]));
				else
					choice_->Append(wxString::Format(wxT(" %i "), std::stoi(v[0])));

				values_.push_back(std::stoi(v[2]));
			}
		}
	}
}

void CharDetailsCustomizationChoice::refresh()
{
	if (choice_)
	{
		uint pos = 0;

		const auto currentValue = details_.get(ID_);

		for (; pos < values_.size(); pos++)
			if (currentValue == values_[pos]) break;

		choice_->SetSelection(pos);

		Layout();
	}
}
