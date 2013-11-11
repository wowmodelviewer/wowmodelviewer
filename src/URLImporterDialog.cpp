/*
 * URLImporterDialog.cpp
 *
 *  Created on: 6 nov. 2013
 *
 */

#include "URLImporterDialog.h"

#include "util.h" // CSConv

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/sstream.h>
#include <wx/statbox.h>
#include <wx/regex.h>
#include <wx/url.h>
#include <wx/html/htmlpars.h>

#include <iostream>

const int URLImporterDialog::ID_BTN_IMPORT = wxNewId();

BEGIN_EVENT_TABLE(URLImporterDialog,wxDialog)
	EVT_BUTTON(ID_BTN_IMPORT,  URLImporterDialog::OnImportButtonClicked)
END_EVENT_TABLE()

class HTMLParser : public wxHtmlParser
{
public:
	wxObject* GetProduct () { return NULL;}
	void AddText(const wxChar* txt) {}
};
/*
wxString URLImporterDialog::extractTagValue(wxString & inputText, wxString & tagValue)
{

}
*/

URLImporterDialog::URLImporterDialog(wxWindow * parent /* = NULL */, wxWindowID id /* = 1 */, const wxString & title /* = "Import from URL" */,
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


void URLImporterDialog::OnImportButtonClicked(wxCommandEvent &event)
{
	if(m_URLname->IsEmpty())
	{
		wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("You must enter an URL before clicking Import !"),
				wxT("No URL given"), wxOK | wxICON_WARNING);
		dial->ShowModal();
	}
	else
	{
		wxURL url(m_URLname->GetValue());
		if(url.GetError()==wxURL_NOERR)
		{
			wxString htmldata;
			wxInputStream *in = url.GetInputStream();

			if(in && in->IsOk())
			{
				wxStringOutputStream html_stream(&htmldata);
				in->Read(html_stream);

				std::string content(html_stream.GetString().ToAscii());

				// let's go : finding name
				// extract global infos
				std::string pattern("(g_npcs[");
				std::string patternEnd(";");
				std::size_t beginIndex = content.find(pattern);
				std::string NPCInfos = content.substr(beginIndex);
				std::size_t endIndex = NPCInfos.find(patternEnd);
				NPCInfos = NPCInfos.substr(0,endIndex);

				// finding name
				pattern = "name\":\"";
				patternEnd = "\",";
				std::string NPCName = NPCInfos.substr(NPCInfos.find(pattern)+pattern.length());
				NPCName = NPCName.substr(0,NPCName.find(patternEnd));

				// finding type
				pattern = "type\":";
				patternEnd = "}";
				std::string NPCType = NPCInfos.substr(NPCInfos.find(pattern)+pattern.length());
				NPCType = NPCType.substr(0,NPCType.find(patternEnd));

				// finding id
				pattern = "id\":";
				patternEnd = ",";
				std::string NPCId = NPCInfos.substr(NPCInfos.find(pattern)+pattern.length());
				NPCId = NPCId.substr(0,NPCId.find(patternEnd));

				// display id
				pattern = "ModelViewer.show({";
				std::string NPCDispId = content.substr(content.find(pattern)+pattern.length());
				pattern = "displayId: ";
				NPCDispId = NPCDispId.substr(NPCDispId.find(pattern)+pattern.length());
				patternEnd = " ";
				NPCDispId = NPCDispId.substr(0,NPCDispId.find(patternEnd));
				if(NPCDispId.find(",") != std::string::npos) // comma at end of id
					NPCDispId = NPCDispId.substr(0,NPCDispId.find(","));

				m_nameResult->SetLabel(CSConv(NPCName));
				m_typeResult->SetLabel(NPCType);
				m_idResult->SetLabel(NPCId);
				m_displayIdResult->SetLabel(NPCDispId);

			}
			delete in;
		}
		else
		{
			wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("URL you entered cannot be reached. Please verify your syntax and your network connection."),
					wxT("URL Error"), wxOK | wxICON_WARNING);
			dial->ShowModal();
		}

	}
}


int URLImporterDialog::getImportedId()
{
	int result = -1;

	if(m_idResult->GetLabel() != "No URL") // successful import
		result = wxAtoi(m_idResult->GetLabel());

	return result;
}

wxString URLImporterDialog::getNPCLine()
{
	wxString result = "";
	if(m_idResult->GetLabel() != "No URL") // successful import
	{
		result = m_idResult->GetLabel();
		result += ",";
		result += m_displayIdResult->GetLabel();
		result += ",";
		result += m_typeResult->GetLabel();
		result += ",";
		result += m_nameResult->GetLabel();
	}

	return result;
}
