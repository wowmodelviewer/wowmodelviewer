/*
 * CharDetailsFrame.cpp
 *
 *  Created on: 21 dec. 2014
 *      Author: Jeromnimo
 */

#include "CharDetailsFrame.h"

#include <iostream>

#include <wx/sizer.h>

#include "charcontrol.h"
#include "CharDetailsCustomizationSpin.h"
#include "enums.h"
#include "globalvars.h"
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

  charCustomizationGS->Clear(true);

  std::vector<CharDetails::CustomizationType> options = m_model->cd.getCustomizationOptions();

  for (auto it = options.begin(), itEnd = options.end(); it != itEnd; ++it)
    charCustomizationGS->Add(new CharDetailsCustomizationSpin(this, m_model->cd, *it), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));

  dhMode->SetValue(false);

  RaceInfos infos;
  if (RaceInfos::getCurrent(model, infos))
  {
    if ((infos.raceid == RACE_NIGHTELF) ||
        (infos.raceid == RACE_BLOODELF))
      dhMode->Enable(true);
    else
      dhMode->Enable(false);
  }

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
  {
    if (m_model->name().contains("bloodelfmale_hd"))
      m_model->mergeModel(QString("item\\objectcomponents\\collections\\demonhuntergeosets_bem.m2"));
    else if (m_model->name().contains("bloodelffemale_hd"))
      m_model->mergeModel(QString("item\\objectcomponents\\collections\\demonhuntergeosets_bef.m2"));
    else if (m_model->name().contains("nightelfmale_hd"))
      m_model->mergeModel(QString("item\\objectcomponents\\collections\\demonhuntergeosets_nim.m2"));
    else if (m_model->name().contains("nightelffemale_hd"))
      m_model->mergeModel(QString("item\\objectcomponents\\collections\\demonhuntergeosets_nif.m2"));
  }
  else
  {
    m_model->unmergeModel();
  }

  m_model->refresh();
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
}
