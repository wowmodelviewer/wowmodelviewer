/*
 * CharDetailsFrame.cpp
 *
 *  Created on: 21 dec. 2014
 *      Author: Jeromnimo
 */

#include "CharDetailsFrame.h"

#include <iostream>

#include <wx/sizer.h>

#include "CharDetailsCustomizationSpin.h"
#include "logger/Logger.h"


IMPLEMENT_CLASS(CharDetailsFrame, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsFrame, wxWindow)
  EVT_BUTTON(wxID_ANY, CharDetailsFrame::onRandomise)
END_EVENT_TABLE()


CharDetailsFrame::CharDetailsFrame(wxWindow* parent)
: wxWindow(parent, wxID_ANY), m_details(0)
{
  LOG_INFO << "Creating CharDetailsFrame...";

  wxFlexGridSizer *top = new wxFlexGridSizer(1);
  top->AddGrowableCol(0);

  charCustomizationGS = new wxFlexGridSizer(1);
  charCustomizationGS->AddGrowableCol(0);
  top->Add(new wxStaticText(this, -1, _("Model Customization"), wxDefaultPosition, wxSize(-1,20), wxALIGN_CENTER),
                            wxSizerFlags().Border(wxBOTTOM, 5).Align(wxALIGN_CENTER));

  top->Add(charCustomizationGS, wxSizerFlags().Border(wxBOTTOM, 5).Expand().Align(wxALIGN_CENTER));
  top->Add(new wxButton(this, wxID_ANY, wxT("Randomise"), wxDefaultPosition, wxDefaultSize), wxSizerFlags().Align(wxALIGN_CENTER).Border(wxALL, 2));
  SetAutoLayout(true);
  top->SetSizeHints(this);
  SetSizer(top);
  Layout();
}

void CharDetailsFrame::setModel(CharDetails & details)
{
  m_details = &details;
 
  charCustomizationGS->Clear(true);

  charCustomizationGS->Add(new CharDetailsCustomizationSpin(this, *m_details, CharDetails::SKIN_COLOR), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
  charCustomizationGS->Add(new CharDetailsCustomizationSpin(this, *m_details, CharDetails::FACE), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
  charCustomizationGS->Add(new CharDetailsCustomizationSpin(this, *m_details, CharDetails::FACIAL_CUSTOMIZATION_STYLE), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
  charCustomizationGS->Add(new CharDetailsCustomizationSpin(this, *m_details, CharDetails::FACIAL_CUSTOMIZATION_COLOR), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
  charCustomizationGS->Add(new CharDetailsCustomizationSpin(this, *m_details, CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));

  SetAutoLayout(true);
  GetSizer()->SetSizeHints(this);
  Layout();
  GetParent()->Layout();
}

void CharDetailsFrame::onRandomise(wxCommandEvent &event)
{
  randomiseChar();
}

void CharDetailsFrame::randomiseChar()
{
  // Choose random values for the looks! ^_^
  m_details->setRandomValue(CharDetails::SKIN_COLOR);
  m_details->setRandomValue(CharDetails::FACE);
  m_details->setRandomValue(CharDetails::FACIAL_CUSTOMIZATION_STYLE);
  m_details->setRandomValue(CharDetails::FACIAL_CUSTOMIZATION_COLOR);
  m_details->setRandomValue(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION);
}
