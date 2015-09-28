/*
 * CharDetailsFrame.cpp
 *
 *  Created on: 21 déc. 2014
 *      Author: Jerome
 */

#include "CharDetailsFrame.h"

#include <iostream>

#include <wx/sizer.h>
#include <wx/spinbutt.h>

#include "logger/Logger.h"
#include "globalvars.h"
#include "GameDatabase.h"
#include "enums.h"
#include "modelviewer.h"

IMPLEMENT_CLASS(CharDetailsFrame, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsFrame, wxWindow)
  EVT_SPIN(ID_SKIN_COLOR, CharDetailsFrame::onSpin)
  EVT_SPIN(ID_FACE_TYPE, CharDetailsFrame::onSpin)
  EVT_SPIN(ID_HAIR_COLOR, CharDetailsFrame::onSpin)
  EVT_SPIN(ID_HAIR_STYLE, CharDetailsFrame::onSpin)
  EVT_SPIN(ID_FACIAL_HAIR, CharDetailsFrame::onSpin)

  EVT_BUTTON(ID_CHAR_RANDOMISE, CharDetailsFrame::onRandomise)
END_EVENT_TABLE()


CharDetailsFrame::CharDetailsFrame(wxWindow* parent)
  : wxWindow(parent, wxID_ANY)
{
  LOG_INFO << "Creating CharDetailsFrame...";

  wxFlexGridSizer *top = new wxFlexGridSizer(1);
  top->AddGrowableCol(0);

  wxGridSizer *gs = new wxGridSizer(3);
  top->Add(new wxStaticText(this, -1, _("Model Customization"), wxDefaultPosition, wxSize(200,20), wxALIGN_CENTER), wxSizerFlags().Border(wxBOTTOM, 5));
  #define ADD_CONTROLS(type, id, caption) \
		gs->Add(new wxStaticText(this, wxID_ANY, caption), wxSizerFlags().Align(wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL)); \
		gs->Add(spins[type] = new wxSpinButton(this, id, wxDefaultPosition, wxSize(30,16), wxSP_HORIZONTAL|wxSP_WRAP), wxSizerFlags(1).Align(wxALIGN_CENTER|wxALIGN_CENTER_VERTICAL)); \
		gs->Add(spinLabels[type] = new wxStaticText(this, wxID_ANY, wxT("0")), wxSizerFlags(2).Align(wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL));

  ADD_CONTROLS(SPIN_SKIN_COLOR, ID_SKIN_COLOR, _("Skin color"))
  ADD_CONTROLS(SPIN_FACE_TYPE, ID_FACE_TYPE, _("Face type"))
  ADD_CONTROLS(SPIN_HAIR_COLOR, ID_HAIR_COLOR, _("Hair color"))
  ADD_CONTROLS(SPIN_HAIR_STYLE, ID_HAIR_STYLE, _("Hair style"))
  ADD_CONTROLS(SPIN_FACIAL_HAIR, ID_FACIAL_HAIR, _("Facial feature"))
  #undef ADD_CONTROLS

  top->Add(gs,wxEXPAND);
  top->Add(new wxButton(this, ID_CHAR_RANDOMISE, wxT("Randomise"), wxDefaultPosition, wxDefaultSize), wxSizerFlags().Align(wxALIGN_CENTER).Border(wxALL, 2));
  SetAutoLayout(true);
  top->SetSizeHints(this);
  SetSizer(top);
  Layout();
}

void CharDetailsFrame::setModel(CharDetails & details)
{
  m_details = &details;
  m_details->attach(this);
}


void CharDetailsFrame::refresh()
{
  if(!m_details)
    return;

  spins[SPIN_SKIN_COLOR]->SetRange(0, (int)m_details->skinColorMax());
  spins[SPIN_FACE_TYPE]->SetRange(0, (int)m_details->faceTypeMax());
  spins[SPIN_HAIR_COLOR]->SetRange(0, (int)m_details->hairColorMax());
  spins[SPIN_HAIR_STYLE]->SetRange(0, (int)m_details->hairStyleMax());
  spins[SPIN_FACIAL_HAIR]->SetRange(0, (int)m_details->facialHairMax());

  spins[SPIN_SKIN_COLOR]->SetValue((int)m_details->skinColor());
  spins[SPIN_FACE_TYPE]->SetValue((int)m_details->faceType());

  std::vector<int> haircols = m_details->validHairColors();
  spins[SPIN_HAIR_COLOR]->SetValue(find(haircols.begin(), haircols.end(),
                                        (int)m_details->hairColor()) - haircols.begin());

  spins[SPIN_HAIR_STYLE]->SetValue((int)m_details->hairStyle());
  spins[SPIN_FACIAL_HAIR]->SetValue((int)m_details->facialHair());

  for (size_t i=0; i<NUM_SPIN_BTNS; i++)
    spinLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), spins[i]->GetValue(), spins[i]->GetMax()));

  for (size_t i=0; i<NUM_SPIN_BTNS; i++)
    spins[i]->Refresh(false);
}

void CharDetailsFrame::onSpin(wxSpinEvent &event)
{
  if(!m_details)
    return;

  switch(event.GetId())
  {
  case ID_SKIN_COLOR:
    m_details->setSkinColor(event.GetPosition());
    break;
  case ID_FACE_TYPE:
    m_details->setFaceType(event.GetPosition());
    break;
  case ID_HAIR_COLOR:
    m_details->setHairColor(m_details->validHairColors()[event.GetPosition()]);
    break;
  case ID_HAIR_STYLE:
    m_details->setHairStyle(event.GetPosition());
    break;
  case ID_FACIAL_HAIR:
    m_details->setFacialHair(event.GetPosition());
    break;
  default:
    break;
  }

  LOG_INFO << "Current model config :"
           << "skinColor" << m_details->skinColor()
           << "faceType" << m_details->faceType()
           << "hairColor" << m_details->hairColor()
           << "hairStyle" << m_details->hairStyle()
           << "facialHair" << m_details->facialHair();
}

void CharDetailsFrame::onRandomise(wxCommandEvent &event)
{
  randomiseChar();
}

void CharDetailsFrame::randomiseChar()
{
  if(!m_details)
    return;

  // Choose random values for the looks! ^_^
  m_details->setSkinColor(randint(0, (int)m_details->skinColorMax()));
  m_details->setFaceType(randint(0, (int)m_details->faceTypeMax()));
  m_details->setHairColor(m_details->validHairColors()[randint(0, m_details->validHairColors().size())]);
  m_details->setHairStyle(randint(0, (int)m_details->hairStyleMax()));
  m_details->setFacialHair(randint(0, (int)m_details->facialHairMax()));
}

void CharDetailsFrame::onEvent(Event *)
{
  refresh();
}
