/*
* CharDetailsFrame.cpp
*
*  Created on: 21 dec. 2014
*      Author: Jeromnimo
*/

#include "CharDetailsFrame.h"

#include <wx/sizer.h>

#include "charcontrol.h"
#include "CharDetailsCustomizationChoice.h"
#include "CharDetailsCustomizationSpin.h"
#include "CharDetailsEvent.h"
#include "Game.h"
#include "WoWModel.h"

#include "logger/Logger.h"


IMPLEMENT_CLASS(CharDetailsFrame, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsFrame, wxWindow)
EVT_BUTTON(wxID_ANY, CharDetailsFrame::onRandomise)
EVT_CHECKBOX(wxID_ANY, CharDetailsFrame::onDHMode)
END_EVENT_TABLE()


CharDetailsFrame::CharDetailsFrame(wxWindow* parent)
: wxWindow(parent, wxID_ANY), m_model(0)
{
  LOG_INFO << "Creating CharDetailsFrame...";

  wxFlexGridSizer *top = new wxFlexGridSizer(1);
  top->AddGrowableCol(0);

  charCustomizationGS = new wxFlexGridSizer(1);
  charCustomizationGS->AddGrowableCol(0);
  top->Add(new wxStaticText(this, -1, _("Model Customization"), wxDefaultPosition, wxSize(-1, 20), wxALIGN_CENTER),
           wxSizerFlags().Border(wxBOTTOM, 5).Align(wxALIGN_CENTER));

  top->Add(charCustomizationGS, wxSizerFlags().Border(wxBOTTOM, 5).Expand().Align(wxALIGN_CENTER));
  top->Add(new wxButton(this, wxID_ANY, wxT("Randomise"), wxDefaultPosition, wxDefaultSize), wxSizerFlags().Align(wxALIGN_CENTER).Border(wxALL, 2));
  dhMode = new wxCheckBox(this, wxID_ANY, wxT("Demon Hunter"), wxDefaultPosition, wxDefaultSize);
  top->Add(dhMode, wxSizerFlags().Align(wxALIGN_CENTER).Border(wxALL, 2));
  SetAutoLayout(true);
  top->SetSizeHints(this);
  SetSizer(top);
  Layout();
}

void CharDetailsFrame::setModel(WoWModel * model)
{
  if (!model)
    return;

  m_model = model;
  m_model->cd.attach(this);

  charCustomizationGS->Clear(true);

  RaceInfos infos;
  RaceInfos::getCurrent(model, infos);

  if(GAMEDIRECTORY.majorVersion() < 9)
  {
    auto options = m_model->cd.getCustomizationOptions();

    for (auto& option : options)
    {
      if (m_model->cd.getParams(option).possibleValues.size() > 1)
        charCustomizationGS->Add(new CharDetailsCustomizationSpin(this, m_model->cd, option), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
    }
  }
  else
  {
    auto options = GAMEDATABASE.sqlQuery(QString("SELECT ID FROM ChrCustomizationOption WHERE ChrModelID = %1 ORDER BY OrderIndex").arg(infos.ChrModelID));

    if(options.valid && !options.values.empty())
    {
      for(auto& option : options.values)
        charCustomizationGS->Add(new CharDetailsCustomizationChoice(this, m_model->cd, option[0].toUInt()), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
    }
  }

  if (infos.raceID == RACE_NIGHTELF || infos.raceID == RACE_BLOODELF)
    dhMode->Enable(true);
  else
    dhMode->Enable(false);

  dhMode->SetValue(model->cd.isDemonHunter());

  SetAutoLayout(true);
  GetSizer()->SetSizeHints(this);
  Layout();
  GetParent()->Layout();
}

void CharDetailsFrame::onRandomise(wxCommandEvent &event)
{
  randomiseChar();
}

void CharDetailsFrame::onDHMode(wxCommandEvent &event)
{
  if (!m_model)
    return;

  if (event.IsChecked())
    m_model->cd.setDemonHunterMode(true);
  else
    m_model->cd.setDemonHunterMode(false);

  setModel(m_model);
  m_model->refresh();
}

void CharDetailsFrame::onEvent(Event * event)
{
  if (event->type() == CharDetailsEvent::DH_MODE_CHANGED)
  {
    dhMode->SetValue(m_model->cd.isDemonHunter());
    setModel(m_model);
  }
}


void CharDetailsFrame::randomiseChar()
{
  if (!m_model)
    return;

  // Choose random values for the looks! ^_^
  m_model->cd.setRandomValue(CharDetails::SKIN_COLOR);
  m_model->cd.setRandomValue(CharDetails::FACE);
  m_model->cd.setRandomValue(CharDetails::FACIAL_CUSTOMIZATION_STYLE);
  m_model->cd.setRandomValue(CharDetails::FACIAL_CUSTOMIZATION_COLOR);
  m_model->cd.setRandomValue(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION);

  // Don't worry about Custom 1-3 for elves, unless they're Demon Hunters:
  RaceInfos infos;
  if (m_model->cd.isDemonHunter() || (RaceInfos::getCurrent(m_model, infos) &&
      infos.raceID != RACE_NIGHTELF && infos.raceID != RACE_BLOODELF))
  {
    m_model->cd.setRandomValue(CharDetails::CUSTOM1_STYLE);
    m_model->cd.setRandomValue(CharDetails::CUSTOM1_COLOR);
    m_model->cd.setRandomValue(CharDetails::CUSTOM2_STYLE);
    m_model->cd.setRandomValue(CharDetails::CUSTOM3_STYLE);
  }
}
