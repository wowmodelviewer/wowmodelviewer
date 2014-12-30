/*
 * CharDetailsFrame.cpp
 *
 *  Created on: 21 déc. 2014
 *      Author: Jerome
 */

#include "CharDetailsFrame.h"

#include "enums.h"
#include "GameDatabase.h"
#include "globalvars.h"
#include "logger/Logger.h"
#include "modelviewer.h"

#include <iostream>

#include <wx/sizer.h>

IMPLEMENT_CLASS(CharDetailsFrame, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsFrame, wxWindow)
  EVT_SPIN(ID_SKIN_COLOR, CharDetailsFrame::onSpin)
  EVT_SPIN(ID_FACE_TYPE, CharDetailsFrame::onSpin)
  EVT_SPIN(ID_HAIR_COLOR, CharDetailsFrame::onSpin)
  EVT_SPIN(ID_HAIR_STYLE, CharDetailsFrame::onSpin)
  EVT_SPIN(ID_FACIAL_HAIR, CharDetailsFrame::onSpin)

  EVT_BUTTON(ID_CHAR_RANDOMISE, CharDetailsFrame::onRandomise)
END_EVENT_TABLE()


CharDetailsFrame::CharDetailsFrame(wxWindow* parent, CharDetails & details)
  : wxWindow(parent, wxID_ANY), m_details(details)
{
  wxLogMessage(wxT("Creating CharDetailsFrame..."));

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

void CharDetailsFrame::refresh()
{
  std::cout << __FUNCTION__ << std::endl;

  m_details.maxFaceType  = g_modelViewer->charControl->getNbValuesForSection(g_canvas->model->isHD?CharSectionsDB::FaceHDType:CharSectionsDB::FaceType);
  m_details.maxSkinColor = g_modelViewer->charControl->getNbValuesForSection(g_canvas->model->isHD?CharSectionsDB::SkinHDType:CharSectionsDB::SkinType);
  m_details.maxHairColor = g_modelViewer->charControl->getNbValuesForSection(g_canvas->model->isHD?CharSectionsDB::HairHDType:CharSectionsDB::HairType);

  QString query = QString("SELECT COUNT(*) FROM CharHairGeoSets WHERE RaceID=%1 AND SexID=%2")
                     .arg(m_details.race)
                     .arg(m_details.gender);

  sqlResult hairStyles = GAMEDATABASE.sqlQuery(query.toStdString());

  if(hairStyles.valid && !hairStyles.values.empty())
  {
    m_details.maxHairStyle = atoi(hairStyles.values[0][0].c_str());
  }
  else
  {
    LOG_ERROR << "Unable to collect number of hair styles for model" << g_canvas->model->name.c_str();
    m_details.maxHairStyle = 0;
  }


  query = QString("SELECT COUNT(*) FROM CharacterFacialHairStyles WHERE RaceID=%1 AND SexID=%2")
                         .arg(m_details.race)
                         .arg(m_details.gender);

  sqlResult facialHairStyles = GAMEDATABASE.sqlQuery(query.toStdString());
  if(facialHairStyles.valid && !facialHairStyles.values.empty())
  {
    m_details.maxFacialHair = atoi(facialHairStyles.values[0][0].c_str());
  }
  else
  {
    LOG_ERROR << "Unable to collect number of facial hair styles for model" << g_canvas->model->name.c_str();
    m_details.maxFacialHair = 0;
  }

  if (m_details.maxFaceType == 0) m_details.maxFaceType = 1;
  if (m_details.maxSkinColor == 0) m_details.maxSkinColor = 1;
  if (m_details.maxHairColor == 0) m_details.maxHairColor = 1;
  if (m_details.maxHairStyle == 0) m_details.maxHairStyle = 1;
  if (m_details.maxFacialHair == 0) m_details.maxFacialHair = 1;


  spins[SPIN_SKIN_COLOR]->SetRange(0, (int)m_details.maxSkinColor-1);
  spins[SPIN_FACE_TYPE]->SetRange(0, (int)m_details.maxFaceType-1);
  spins[SPIN_HAIR_COLOR]->SetRange(0, (int)m_details.maxHairColor-1);
  spins[SPIN_HAIR_STYLE]->SetRange(0, (int)m_details.maxHairStyle-1);
  spins[SPIN_FACIAL_HAIR]->SetRange(0, (int)m_details.maxFacialHair-1);

  spins[SPIN_SKIN_COLOR]->SetValue((int)m_details.skinColor);
  spins[SPIN_FACE_TYPE]->SetValue((int)m_details.faceType);
  spins[SPIN_HAIR_COLOR]->SetValue((int)m_details.hairColor);
  spins[SPIN_HAIR_STYLE]->SetValue((int)m_details.hairStyle);
  spins[SPIN_FACIAL_HAIR]->SetValue((int)m_details.facialHair);

  for (size_t i=0; i<NUM_SPIN_BTNS; i++)
    spinLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), spins[i]->GetValue(), spins[i]->GetMax()));

  for (size_t i=0; i<NUM_SPIN_BTNS; i++)
    spins[i]->Refresh(false);
}

void CharDetailsFrame::onSpin(wxSpinEvent &event)
{
  if (!g_canvas)
    return;

  if (event.GetId()==ID_SKIN_COLOR)
  {
    m_details.skinColor = event.GetPosition();
    // check if underwear texture is available for this color
    // if no, uncheck option, and make menu unavailable
    if(g_modelViewer->charControl->getTextureNameForSection(g_canvas->model->isHD?CharSectionsDB::UnderwearHDType:CharSectionsDB::UnderwearType).size() == 0)
    {
      m_details.showUnderwear = false;
      g_modelViewer->charMenu->Check(ID_SHOW_UNDERWEAR, false);
      g_modelViewer->charMenu->Enable(ID_SHOW_UNDERWEAR, false);
    }
    else
    {
      g_modelViewer->charMenu->Enable(ID_SHOW_UNDERWEAR, true);
    }

    // update facial type based on skin chosen
    m_details.maxFaceType  = g_modelViewer->charControl->getNbValuesForSection(g_canvas->model->isHD?CharSectionsDB::FaceHDType:CharSectionsDB::FaceType);

    if(m_details.faceType > m_details.maxFaceType)
      m_details.faceType = 0;

    refresh();
  }

  if(g_canvas->model->modelType == MT_NPC)
    return;

  if (event.GetId()==ID_FACE_TYPE)
    m_details.faceType = event.GetPosition();
  else if (event.GetId()==ID_HAIR_COLOR) {
    m_details.hairColor = event.GetPosition();
  } else if (event.GetId()==ID_HAIR_STYLE)
    m_details.hairStyle = event.GetPosition();
  else if (event.GetId()==ID_FACIAL_HAIR)
    m_details.facialHair = event.GetPosition();

  for (size_t i=0; i<NUM_SPIN_BTNS; i++)
    spinLabels[i]->SetLabel(wxString::Format(wxT("%i / %i"), spins[i]->GetValue(), spins[i]->GetMax()));

  LOG_INFO << "Current model config :"
           << "skinColor" << m_details.skinColor
           << "faceType" << m_details.faceType
           << "hairColor" << m_details.hairColor
           << "hairStyle" << m_details.hairStyle
           << "facialHair" << m_details.facialHair;
  g_modelViewer->charControl->RefreshModel();
}

void CharDetailsFrame::onRandomise(wxCommandEvent &event)
{
  randomiseChar();
  g_modelViewer->charControl->RefreshModel();
}

void CharDetailsFrame::randomiseChar()
{
  // Choose random values for the looks! ^_^
  m_details.skinColor = randint(0, (int)m_details.maxSkinColor-1);
  m_details.faceType = randint(0, (int)m_details.maxFaceType-1);
  m_details.hairColor = randint(0, (int)m_details.maxHairColor-1);
  m_details.hairStyle = randint(0, (int)m_details.maxHairStyle-1);
  m_details.facialHair = randint(0, (int)m_details.maxFacialHair-1);

  refresh();
}
