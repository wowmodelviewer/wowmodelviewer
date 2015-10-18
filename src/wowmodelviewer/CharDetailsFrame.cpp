/*
 * CharDetailsFrame.cpp
 *
 *  Created on: 21 déc. 2014
 *      Author: Jerome
 */

#include "CharDetailsFrame.h"

#include <iostream>

#include <wx/sizer.h>

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

  wxFlexGridSizer *gs = new wxFlexGridSizer(3, 0, 5);
  gs->AddGrowableCol(2);
  top->Add(new wxStaticText(this, -1, _("Model Customization"), wxDefaultPosition, wxSize(-1,20), wxALIGN_CENTER),
                            wxSizerFlags().Border(wxBOTTOM, 5).Align(wxALIGN_CENTER));
  #define ADD_CONTROLS(type, id, caption) \
		gs->Add(new wxStaticText(this, wxID_ANY, caption), \
                        wxSizerFlags().Align(wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL). \
                        Border(wxLEFT, 5)); \
		gs->Add(spins[type] = new wxSpinButton(this, id, wxDefaultPosition, wxSize(30,16), \
                        wxSP_HORIZONTAL|wxSP_WRAP), wxSizerFlags().Align(wxALIGN_CENTER|wxALIGN_CENTER_VERTICAL)); \
		gs->Add(spinLabels[type] = new wxStaticText(this, wxID_ANY, wxT("00 / 00")), \
                        wxSizerFlags().Align(wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL));

  ADD_CONTROLS(SPIN_SKIN_COLOR, ID_SKIN_COLOR, _("Skin color:"))
  ADD_CONTROLS(SPIN_FACE_TYPE, ID_FACE_TYPE, _("Face type:"))
  ADD_CONTROLS(SPIN_HAIR_COLOR, ID_HAIR_COLOR, _("Hair color:"))
  ADD_CONTROLS(SPIN_HAIR_STYLE, ID_HAIR_STYLE, _("Hair style:"))
  ADD_CONTROLS(SPIN_FACIAL_HAIR, ID_FACIAL_HAIR, _("Facial feature:"))
  #undef ADD_CONTROLS

  top->Add(gs,wxSizerFlags().Border(wxBOTTOM, 5).Expand());
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
  int val, max, index;
  std::vector<int> validVals;

  for (size_t i=0; i<NUM_SPIN_BTNS; i++)
  {
    switch (i)
    {
      case SPIN_FACE_TYPE   : validVals = m_details->validFaceTypes();
                              val = (int)m_details->faceType();
                              index = find(validVals.begin(), validVals.end(), val) - validVals.begin();
                              max = validVals.size() - 1;
                              break;
      case SPIN_HAIR_COLOR  : validVals = m_details->validHairColors();
                              val = (int)m_details->hairColor();
                              index = find(validVals.begin(), validVals.end(), val) - validVals.begin();
                              max = validVals.size() - 1;
                              break;
      case SPIN_SKIN_COLOR  : val = (int)m_details->skinColor();
                              index = val;
                              max = (int)m_details->skinColorMax();
                              break;
      case SPIN_HAIR_STYLE  : val = (int)m_details->hairStyle();
                              index = val;
                              max = (int)m_details->hairStyleMax();
                              break;
      case SPIN_FACIAL_HAIR : val = (int)m_details->facialHair();
                              index = val;
                              max = (int)m_details->facialHairMax();
                              break;
    }
    spins[i]->SetRange(0, max);
    spins[i]->SetValue(index);
    spinLabels[i]->SetLabel(wxString::Format(wxT("%2i / %i"), index + 1, max + 1));
    spins[i]->Refresh(false);
  }
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
    m_details->setFaceType(m_details->validFaceTypes()[event.GetPosition()]);
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
  if (m_details->validFaceTypes().size())
    m_details->setFaceType(m_details->validFaceTypes()[randint(0, m_details->validFaceTypes().size() - 1)]);
  if (m_details->validHairColors().size())
    m_details->setHairColor(m_details->validHairColors()[randint(0, m_details->validHairColors().size() - 1)]);
  m_details->setHairStyle(randint(0, (int)m_details->hairStyleMax()));
  m_details->setFacialHair(randint(0, (int)m_details->facialHairMax()));
}

void CharDetailsFrame::onEvent(Event *)
{
  refresh();
}
