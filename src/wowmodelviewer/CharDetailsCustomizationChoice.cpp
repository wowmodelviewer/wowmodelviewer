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
: wxWindow(parent, wxID_ANY), m_ID(chrCustomizationChoiceID), m_details(details)
{
  wxFlexGridSizer *top = new wxFlexGridSizer(2, 0, 5);
  top->AddGrowableCol(2);

  m_details.attach(this);

  auto option = GAMEDATABASE.sqlQuery(QString("SELECT Name FROM ChrCustomizationOption WHERE ID = %1").arg(m_ID));

  if(option.valid && !option.values.empty())
  {
    top->Add(new wxStaticText(this, wxID_ANY, option.values[0][0].toStdWString()),
      wxSizerFlags().Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

    m_choice = new wxChoice(this, wxID_ANY);

    top->Add(m_choice, wxSizerFlags().Align(wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

    SetAutoLayout(true);
    top->SetSizeHints(this);
    SetSizer(top);

    buildList();
  }

  m_details.set(m_ID, m_values[0]);
  refresh();
  Layout();
}

void CharDetailsCustomizationChoice::onChoice(wxCommandEvent& event)
{
  LOG_INFO << __FUNCTION__ << event.GetSelection();
  m_details.set(m_ID, m_values[event.GetSelection()]);
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
  if (m_choice)
  {
    // clear list and re add possible choices
    m_choice->Clear();
    m_values.clear();

    auto choices = GAMEDATABASE.sqlQuery(QString("SELECT OrderIndex,Name,ID FROM ChrCustomizationChoice WHERE ChrCustomizationOptionID = %1 ORDER BY OrderIndex").arg(m_ID));

    if(choices.valid && !choices.values.empty())
    {
      const auto useName = !choices.values[0][1].isEmpty();

      for(auto v:choices.values)
      {
        if(useName)
          m_choice->Append(wxString(v[1].toStdString().c_str(), wxConvUTF8));
        else
          m_choice->Append(wxString::Format(wxT(" %i "), v[0].toInt()));

        m_values.push_back(v[2].toUInt());
      }
    }
  }
}


void CharDetailsCustomizationChoice::refresh()
{
  if (m_choice)
  {
    uint pos = 0;

    const auto currentValue = m_details.get(m_ID);

    for (; pos < m_values.size(); pos++)
      if (currentValue == m_values[pos]) break;

    m_choice->SetSelection(pos);
  }
}


