/*
 * NPCimporterDialog.cpp
 *
 *  Created on: 6 nov. 2013
 *
 */

#include "NPCImporterDialog.h"

#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "ImporterPlugin.h"
#include "NPCInfos.h"
#include "PluginManager.h"
#include "metaclasses/Iterator.h"

const int NPCimporterDialog::ID_BTN_IMPORT = wxNewId();

BEGIN_EVENT_TABLE(NPCimporterDialog,wxDialog)
	EVT_BUTTON(ID_BTN_IMPORT,  NPCimporterDialog::OnImportButtonClicked)
END_EVENT_TABLE()


NPCimporterDialog::NPCimporterDialog(wxWindow * parent /* = NULL */, wxWindowID id /* = 1 */, const wxString & title /* = "Import from URL" */,
        							 const wxPoint & position /* = wxDefaultPosition */, const wxSize & size /*= wxSize(300, 300) */)
: wxDialog( parent, id, title, position, size, wxRAISED_BORDER|wxDEFAULT_DIALOG_STYLE|wxCAPTION|wxTHICK_FRAME|wxSYSTEM_MENU )
{
	wxBoxSizer *mainsizer = new wxBoxSizer(wxVERTICAL);

	// up part : some explanation + url import choice
	wxStaticBoxSizer *topSizer = new wxStaticBoxSizer(wxVERTICAL, this, _T("Import parameters"));
	wxStaticText * explain =  new wxStaticText(this, wxID_ANY, _T("Put in the field below wowhead page of wanted NPC.\nURL scheme must be like http://XXX.wowhead.com/npc=number\n(where XXX = www or any supported wowhead language)\nWhen done, click import. If everything succeed, just click on Display button to show model in viewer."));
	topSizer->Add(explain, 0, wxALL, 5);
	wxStaticText *label = new wxStaticText(this, wxID_ANY, _T("URL :"));
	topSizer->Add(label, 0, wxLEFT|wxRIGHT|wxTOP, 5);
	wxBoxSizer *URLSizer = new wxBoxSizer(wxHORIZONTAL);
	m_URLname = new wxTextCtrl(this, wxID_ANY, _T(""));
	m_URLname->SetMinSize(wxSize(200,10));
	URLSizer->Add(m_URLname, 0, wxLEFT|wxRIGHT|wxBOTTOM|wxEXPAND, 5);
	m_importBtn = new wxButton( this, ID_BTN_IMPORT, _("Import"));
	URLSizer->Add(m_importBtn, 0, wxLEFT|wxRIGHT|wxBOTTOM, 5);
	topSizer->Add(URLSizer,0, wxEXPAND);

	// lower part : query result
	wxStaticBoxSizer *bottomSizer = new wxStaticBoxSizer(wxVERTICAL, this, _T("Import results"));

	// name
	wxBoxSizer *nameSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText *nameLabel = new wxStaticText(this, wxID_ANY, _T("Name :"));
	nameSizer->Add(nameLabel, 0, wxLEFT|wxRIGHT|wxTOP, 5);
	m_nameResult = new wxStaticText(this, wxID_ANY, _T("No URL"));
	nameSizer->Add(m_nameResult, 0, wxALL, 5);
	bottomSizer->Add(nameSizer,0, wxEXPAND);

	// type
	wxBoxSizer *typeSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText *typeLabel = new wxStaticText(this, wxID_ANY, _T("Type :"));
	typeSizer->Add(typeLabel, 0, wxLEFT|wxRIGHT|wxTOP, 5);
	m_typeResult = new wxStaticText(this, wxID_ANY, _T("No URL"));
	typeSizer->Add(m_typeResult, 0, wxALL, 5);
	bottomSizer->Add(typeSizer,0, wxEXPAND);

	// ids (id + display Id)
	wxBoxSizer *idSizer = new wxBoxSizer(wxHORIZONTAL);
	// id
	wxStaticText *idLabel = new wxStaticText(this, wxID_ANY, _T("Id :"));
	idSizer->Add(idLabel, 0, wxLEFT|wxRIGHT|wxTOP, 5);
	m_idResult = new wxStaticText(this, wxID_ANY, wxT("No URL"));
	idSizer->Add(m_idResult, 0, wxALL, 5);
	// didplay id
	wxStaticText *displayIdLabel = new wxStaticText(this, wxID_ANY, _T("Display Id :"));
	idSizer->Add(displayIdLabel, 0, wxLEFT|wxRIGHT|wxTOP, 5);
	m_displayIdResult = new wxStaticText(this, wxID_ANY, _T("No URL"));
	idSizer->Add(m_displayIdResult, 0, wxALL, 5);
	bottomSizer->Add(idSizer,0, wxEXPAND);


	// OK / Cancel part
	wxBoxSizer *buttonsBox = new wxBoxSizer(wxHORIZONTAL);
	buttonsBox->Add(new wxButton( this, wxID_OK, _("Display"), wxDefaultPosition, wxDefaultSize));
	buttonsBox->Add(new wxButton( this, wxID_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize));

	// main panel adds
	mainsizer->Add(topSizer, 0, wxALL|wxEXPAND, 5);
	mainsizer->Add(bottomSizer, 0, wxALL|wxEXPAND, 5);
	mainsizer->Add(buttonsBox, 0, wxALIGN_RIGHT | wxALIGN_BOTTOM | wxALL, 5);

	SetSizer(mainsizer);
	mainsizer->SetSizeHints(this);

}


void NPCimporterDialog::OnImportButtonClicked(wxCommandEvent &event)
{
	if(m_URLname->IsEmpty())
	{
		wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("You must enter an URL before clicking Import !"),
				wxT("No URL given"), wxOK | wxICON_WARNING);
		dial->ShowModal();
	}
	else
	{
	  std::string url = m_URLname->GetValue().ToAscii();
	  Iterator<ImporterPlugin> pluginIt(PLUGINMANAGER);
	  NPCInfos * result = NULL;
	  for(pluginIt.begin(); !pluginIt.ended() ; pluginIt++)
	  {
	    ImporterPlugin * plugin = *pluginIt;
	    if(plugin->acceptURL(url))
	    {
	      result = plugin->importNPC(url);
	    }
	  }

		if(result)
		{
			m_nameResult->SetLabel(wxString(result->name.c_str()));
			m_typeResult->SetLabel(wxString::Format(wxT("%i"),result->type));
			m_idResult->SetLabel(wxString::Format(wxT("%i"),result->id));
			m_displayIdResult->SetLabel(wxString::Format(wxT("%i"),result->displayId));
			delete result;
		}
		else
		{
			wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("URL you entered cannot be reached. Please verify your syntax and your network connection."),
					wxT("URL Error"), wxOK | wxICON_WARNING);
			dial->ShowModal();
		}
	}
}


int NPCimporterDialog::getImportedId()
{
	int result = -1;

	if(m_idResult->GetLabel() != "No URL") // successful import
		result = wxAtoi(m_idResult->GetLabel());

	return result;
}

QString NPCimporterDialog::getNPCLine()
{
	QString result = "";
	if(m_idResult->GetLabel() != "No URL") // successful import
	{
		result = m_idResult->GetLabel().c_str();
		result += ",";
		result += m_displayIdResult->GetLabel().c_str();
		result += ",";
		result += m_typeResult->GetLabel().c_str();
		result += ",";
		result += m_nameResult->GetLabel().c_str();
	}

	return result;
}
