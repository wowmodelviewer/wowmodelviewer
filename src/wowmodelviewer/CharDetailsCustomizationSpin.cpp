/*
* CharDetailsCustomizationSpin.cpp
*
*  Created on: 21 dec. 2014
*      Author: Jeromnimo
*/

#include "CharDetailsCustomizationSpin.h"

#include "CharDetails.h"

#include "logger/Logger.h"

IMPLEMENT_CLASS(CharDetailsCustomizationSpin, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsCustomizationSpin, wxWindow)
  EVT_SPIN(wxID_ANY, CharDetailsCustomizationSpin::onSpin)
END_EVENT_TABLE()


CharDetailsCustomizationSpin::CharDetailsCustomizationSpin(wxWindow* parent, CharDetails & details, CharDetails::CustomizationType type)
: wxWindow(parent, wxID_ANY),  m_type(type), m_details(details)
{
  wxFlexGridSizer *top = new wxFlexGridSizer(3, 0, 5);
  top->AddGrowableCol(2);

  m_params = m_details.getParams(m_type);
  m_details.attach(this);

  top->Add(new wxStaticText(this, wxID_ANY, m_params.name.c_str()),
           wxSizerFlags().Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

  m_spin = new wxSpinButton(this, wxID_ANY, wxDefaultPosition, wxSize(30, 16), wxSP_HORIZONTAL | wxSP_WRAP);
  m_spin->SetRange(0, m_params.possibleValues.size() - 1);
  top->Add(m_spin, wxSizerFlags().Align(wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

  m_text = new wxStaticText(this, wxID_ANY, wxString::Format(wxT("%2i / %i"), 0, m_params.possibleValues.size() - 1));
  top->Add(m_text, wxSizerFlags().Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 10));

  SetAutoLayout(true);
  top->SetSizeHints(this);
  SetSizer(top);
  Layout();
}


void CharDetailsCustomizationSpin::onSpin(wxSpinEvent &event)
{
  m_details.set(m_type, m_params.possibleValues[event.GetPosition()]);
}

void CharDetailsCustomizationSpin::onEvent(Event *)
{
  if (m_text)
  {
    uint pos = 0;
    uint currentValue = m_details.get(m_type);

    for (; pos < m_params.possibleValues.size(); pos++)
      if (m_params.possibleValues[pos] == currentValue) break;

    m_spin->SetValue(currentValue);

    m_text->SetLabel(wxString::Format(wxT("%2i / %i"), pos, m_params.possibleValues.size() - 1));
  }
}


