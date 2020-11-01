/*
* CharDetailsCustomizationChoice.cpp
*
*  Created on: 27 sep. 2020
*      Author: Jeromnimo
*/

#include "CharDetailsCustomizationChoice.h"

#include "CharDetails.h"
#include "CharDetailsEvent.h"
#include "Game.h"

#include "logger/Logger.h"

IMPLEMENT_CLASS(CharDetailsCustomizationChoice, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsCustomizationChoice, wxWindow)
  EVT_CHOICE(wxID_ANY, CharDetailsCustomizationChoice::onChoice)
END_EVENT_TABLE()


CharDetailsCustomizationChoice::CharDetailsCustomizationChoice(wxWindow* parent, CharDetails & details, uint chrCustomizationChoiceID)
: wxWindow(parent, wxID_ANY), ID_(chrCustomizationChoiceID), details_(details)
{
  auto top = new wxFlexGridSizer(2, 0, 5);
  top->AddGrowableCol(2);

  details_.attach(this);

  auto option = GAMEDATABASE.sqlQuery(QString("SELECT Name FROM ChrCustomizationOption WHERE ID = %1").arg(ID_));

  if(option.valid && !option.values.empty())
  {
    top->Add(new wxStaticText(this, wxID_ANY, option.values[0][0].toStdWString()),
      wxSizerFlags().Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

    choice_ = new wxChoice(this, wxID_ANY);

    top->Add(choice_, wxSizerFlags().Align(wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

    SetAutoLayout(true);
    top->SetSizeHints(this);
    SetSizer(top);

    buildList();
  }

  if(!values_.empty())
    details_.set(ID_, values_[0]);
  
  refresh();
}

void CharDetailsCustomizationChoice::onChoice(wxCommandEvent& event)
{
  LOG_INFO << __FUNCTION__ << event.GetSelection();
  details_.set(ID_, values_[event.GetSelection()]);
}

void CharDetailsCustomizationChoice::onEvent(Event * e)
{
  /*
  // update params for dynamically changing customization stuff (ie face type depends on skin color)
  if ((e->type() == CharDetailsEvent::SKIN_COLOR_CHANGED && m_type == CharDetails::FACE) ||
      (e->type() == CharDetailsEvent::FACE_CHANGED && m_type == CharDetails::SKIN_COLOR))
  {
    buildList();
    refresh();
  }

  // refesh only if event corresponds to current customization type (avoid flicker effect)
  if((e->type() == CharDetailsEvent::SKIN_COLOR_CHANGED && m_type == CharDetails::SKIN_COLOR) ||
     (e->type() == CharDetailsEvent::FACE_CHANGED && m_type == CharDetails::FACE) ||
     (e->type() == CharDetailsEvent::FACIAL_CUSTOMIZATION_STYLE_CHANGED && m_type == CharDetails::FACIAL_CUSTOMIZATION_STYLE) ||
     (e->type() == CharDetailsEvent::FACIAL_CUSTOMIZATION_COLOR_CHANGED && m_type == CharDetails::FACIAL_CUSTOMIZATION_COLOR) ||
     (e->type() == CharDetailsEvent::ADDITIONAL_FACIAL_CUSTOMIZATION_CHANGED && m_type == CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION) ||
     (e->type() == CharDetailsEvent::CUSTOM1_STYLE_CHANGED && m_type == CharDetails::CUSTOM1_STYLE) ||
     (e->type() == CharDetailsEvent::CUSTOM1_COLOR_CHANGED && m_type == CharDetails::CUSTOM1_COLOR) ||
     (e->type() == CharDetailsEvent::CUSTOM2_STYLE_CHANGED && m_type == CharDetails::CUSTOM2_STYLE) ||
     (e->type() == CharDetailsEvent::CUSTOM3_STYLE_CHANGED && m_type == CharDetails::CUSTOM3_STYLE))
  {
    refresh();
  }
  */

}

void CharDetailsCustomizationChoice::buildList()
{
  if (choice_)
  {
    // clear list and re add possible choices
    choice_->Clear();
    values_.clear();

    auto ids = details_.getCustomisationChoices(ID_);

    if (ids.empty())
      return;

    auto query = QString("SELECT OrderIndex,Name,ID FROM ChrCustomizationChoice WHERE ID IN (");
    for(auto id:ids)
    {
      query += QString::number(id);
      query += ",";
    }

    query.chop(1);
    query += ") ORDER BY OrderIndex";

    auto choices = GAMEDATABASE.sqlQuery(query);

    if(choices.valid && !choices.values.empty())
    {
      for(auto v:choices.values)
      {
        if(!v[1].isEmpty())
          choice_->Append(wxString(v[1].toStdString().c_str(), wxConvUTF8));
        else
          choice_->Append(wxString::Format(wxT(" %i "), v[0].toInt()));

        values_.push_back(v[2].toUInt());
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


