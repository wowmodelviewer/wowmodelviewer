/*
* CharDetailsCustomizationSpin.cpp
*
*  Created on: 21 dec. 2014
*      Author: Jeromnimo
*/

#include "CharDetailsCustomizationSpin.h"

#include "CharDetails.h"

#include <wx/spinbutt.h>

IMPLEMENT_CLASS(CharDetailsCustomizationSpin, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsCustomizationSpin, wxWindow)
  EVT_SPIN(wxID_ANY, CharDetailsCustomizationSpin::onSpin)
END_EVENT_TABLE()


CharDetailsCustomizationSpin::CharDetailsCustomizationSpin(wxWindow* parent, CharDetails * details, CharDetails::CustomizationType type)
  : wxWindow(parent, wxID_ANY), m_type(type)
{
  wxFlexGridSizer *top = new wxFlexGridSizer(3, 0, 5);
  top->AddGrowableCol(2);

  CharDetails::CustomizationParam params;

  if (details)
    params = details->getParams(m_type);

  top->Add(new wxStaticText(this, wxID_ANY, params.name.c_str()),
           wxSizerFlags().Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));
  top->Add(new wxSpinButton(this, wxID_ANY, wxDefaultPosition, wxSize(30, 16),
    wxSP_HORIZONTAL | wxSP_WRAP), wxSizerFlags().Align(wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));
  top->Add(new wxStaticText(this, wxID_ANY, QString("0/%1").arg(params.possibleValues.size()-1).toStdString()),
           wxSizerFlags().Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 10));

  SetAutoLayout(true);
  top->SetSizeHints(this);
  SetSizer(top);
  Layout();
}


void CharDetailsCustomizationSpin::onSpin(wxSpinEvent &event)
{

}


