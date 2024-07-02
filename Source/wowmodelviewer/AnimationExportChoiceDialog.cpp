#include "AnimationExportChoiceDialog.h"
#include <iostream>
#include <fbxsdk/fileio/fbx/fbxio.h>

const int ID_SELECT_ALL = wxNewId();
const int ID_UNSELECT_ALL = wxNewId();

enum
{
	wxID_LISTBOX = 3000 // from choicedgg.cpp
};

BEGIN_EVENT_TABLE(AnimationExportChoiceDialog, wxMultiChoiceDialog)
	EVT_CHECKLISTBOX(wxID_LISTBOX, AnimationExportChoiceDialog::updateButtons)
	EVT_BUTTON(ID_SELECT_ALL, AnimationExportChoiceDialog::OnSelectAll)
	EVT_BUTTON(ID_UNSELECT_ALL, AnimationExportChoiceDialog::OnUnselectAll)
END_EVENT_TABLE()

AnimationExportChoiceDialog::AnimationExportChoiceDialog(wxWindow* parent, const wxString& message,
                                                         const wxString& caption, const wxArrayString& choices)
	: wxMultiChoiceDialog(parent, message, caption, choices)
{
	wxSizer* topsizer = GetSizer();

	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

	wxStaticText* explain = new wxStaticText(this, wxID_ANY, wxT("Select animations you want to export"));

	// @TODO : to remove once bug corrected
	// wxStaticText * bugexplain =  new wxStaticText(this, wxID_ANY, wxT("Due to FBX export bug, export only one at a time for now"));

	// wxStaticText * fbxversionexplain = new wxStaticText(this, wxID_ANY, wxT("Select an FBX Compatibility Version"));
	// wxComboBox * fbxVersionChoice = new wxComboBox(this, wxID_ANY);
	// fbxVersionChoice->SetEditable(false);
	// fbxVersionChoice->SetLabel(wxT("FBX Compatibility Version"));
	// int i = 0;
	// 
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2019_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2018_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2016_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2014_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2013_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2012_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2011_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2010_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2009_00_V7_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2009_00_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2006_11_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2006_08_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2006_02_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_2005_08_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_60_COMPATIBLE), i++);
	// fbxVersionChoice->Insert(wxString::FromAscii(FBX_53_MB55_COMPATIBLE), i++);
	// 
	// fbxVersionChoice->SetSelection(0);

	m_selectall = new wxButton(this, ID_SELECT_ALL,_("Select all"));
	m_unselectall = new wxButton(this, ID_UNSELECT_ALL,_("Unselect all"));
	sizer->Add(m_selectall, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
	sizer->Add(m_unselectall, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	topsizer->Prepend(sizer, 0, wxEXPAND | wxALL, 0);
	//topsizer->Prepend(bugexplain, 0, wxALL, 5);
	//topsizer->Prepend(fbxVersionChoice, 0, wxALL, 5);
	//topsizer->Prepend(fbxversionexplain, 0, wxALL, 5);
	topsizer->Prepend(explain, 0, wxALL, 5);
	topsizer->SetSizeHints(this);
	topsizer->Fit(this);

	// by default everything is selected
	m_selectall->Enable(false);
	m_unselectall->Enable(true);
}

void AnimationExportChoiceDialog::updateButtons(wxCommandEvent&)
{
	unsigned int nbselected = 0;

	wxCheckListBox* checkListBox = wxDynamicCast(m_listbox, wxCheckListBox);

	for (unsigned int n = 0; n < checkListBox->GetCount(); n++)
	{
		if (checkListBox->IsChecked(n))
			nbselected++;
	}

	if (m_listbox->GetCount() == nbselected)
	{
		m_selectall->Enable(false);
		m_unselectall->Enable(true);
	}
	else if (nbselected == 0)
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

	for (unsigned int n = 0; n < checkListBox->GetCount(); n++)
	{
		if (!checkListBox->IsChecked(n))
			checkListBox->Check(n, true);
	}

	m_selectall->Enable(false);
	m_unselectall->Enable(true);
}

void AnimationExportChoiceDialog::OnUnselectAll(wxCommandEvent&)
{
	wxCheckListBox* checkListBox = wxDynamicCast(m_listbox, wxCheckListBox);

	for (unsigned int n = 0; n < checkListBox->GetCount(); n++)
	{
		if (checkListBox->IsChecked(n))
			checkListBox->Check(n, false);
	}

	m_selectall->Enable(true);
	m_unselectall->Enable(false);
}
