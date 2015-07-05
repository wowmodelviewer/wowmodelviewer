/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * AnimationExportChoiceDialog.cpp
 *
 *  Created on: 3 jul. 2015
 *   Copyright: 2015, WoW Model Viewer (http://wowmodelviewer.net)
 */

#define _ANIMATIONEXPORTCHOICEDIALOG_CPP_
#include "AnimationExportChoiceDialog.h"
#undef _ANIMATIONEXPORTCHOICEDIALOG_CPP_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL
#include <iostream>

// Qt

// Externals

// Other libraries

// Current library

// Namespaces used
//--------------------------------------------------------------------


// Beginning of implementation
//====================================================================
const int ID_SELECT_ALL = wxNewId();
const int ID_UNSELECT_ALL = wxNewId();
#define wxID_LISTBOX 3000 // from choicedgg.cpp

BEGIN_EVENT_TABLE(AnimationExportChoiceDialog, wxMultiChoiceDialog)
  EVT_CHECKLISTBOX(wxID_LISTBOX, AnimationExportChoiceDialog::updateButtons)
  EVT_BUTTON(ID_SELECT_ALL,  AnimationExportChoiceDialog::OnSelectAll)
  EVT_BUTTON(ID_UNSELECT_ALL,  AnimationExportChoiceDialog::OnUnselectAll)
END_EVENT_TABLE()

// Constructors
//--------------------------------------------------------------------
AnimationExportChoiceDialog::AnimationExportChoiceDialog(wxWindow *parent, const wxString &message, const wxString &caption, const wxArrayString &choices)
 : wxMultiChoiceDialog (parent, message, caption, choices)
{
  wxSizer *topsizer = GetSizer();

  wxBoxSizer *sizer = new wxBoxSizer( wxHORIZONTAL );

  wxStaticText * explain =  new wxStaticText(this, wxID_ANY, wxT("Select animations you want to export"));

  // @TODO : to remove once bug corrected
  wxStaticText * bugexplain =  new wxStaticText(this, wxID_ANY, wxT("Due to FBX export bug, export only one at a time for now"));


  m_selectall = new wxButton(this, ID_SELECT_ALL,_("Select all"));
  m_unselectall = new wxButton(this, ID_UNSELECT_ALL,_("Unselect all"));
  sizer->Add(m_selectall, 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);
  sizer->Add(m_unselectall, 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);

  topsizer->Prepend(sizer, 0, wxEXPAND | wxALL, 0);
  topsizer->Prepend(bugexplain, 0, wxALL, 5);
  topsizer->Prepend(explain, 0, wxALL, 5);
  topsizer->SetSizeHints( this );
  topsizer->Fit( this );

  // by default everything is selected
  m_selectall->Enable(false);
  m_unselectall->Enable(true);
}

// Destructor
//--------------------------------------------------------------------


// Public methods
//--------------------------------------------------------------------


// Protected methods
//--------------------------------------------------------------------


// Private methods
//--------------------------------------------------------------------
void AnimationExportChoiceDialog::updateButtons(wxCommandEvent&)
{
  unsigned int nbselected = 0;

  wxCheckListBox* checkListBox = wxDynamicCast(m_listbox, wxCheckListBox);

  for (unsigned int n = 0; n < checkListBox->GetCount(); n++ )
  {
    if(checkListBox->IsChecked(n))
      nbselected++;
  }

  if(m_listbox->GetCount() == nbselected)
  {
    m_selectall->Enable(false);
    m_unselectall->Enable(true);
  }
  else if(nbselected == 0)
  {
    m_selectall->Enable(true);
    m_unselectall->Enable(false);
  }
  else
  {
    m_selectall->Enable(true);
    m_unselectall->Enable(true);
  }
}

void AnimationExportChoiceDialog::OnSelectAll(wxCommandEvent&)
{
  wxCheckListBox* checkListBox = wxDynamicCast(m_listbox, wxCheckListBox);

  for (unsigned int n = 0; n < checkListBox->GetCount(); n++ )
  {
    if(!checkListBox->IsChecked(n))
      checkListBox->Check(n,true);
  }

  m_selectall->Enable(false);
  m_unselectall->Enable(true);
}

void AnimationExportChoiceDialog::OnUnselectAll(wxCommandEvent&)
{
  wxCheckListBox* checkListBox = wxDynamicCast(m_listbox, wxCheckListBox);

  for (unsigned int n = 0; n < checkListBox->GetCount(); n++ )
  {
    if(checkListBox->IsChecked(n))
      checkListBox->Check(n,false);
  }

  m_selectall->Enable(true);
  m_unselectall->Enable(false);
}

