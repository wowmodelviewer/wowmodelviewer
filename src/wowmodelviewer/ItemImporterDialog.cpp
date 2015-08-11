/*
 * ItemImporterDialog.cpp
 *
 *  Created on: 14 dec. 2013
 *
 */

#include "ItemImporterDialog.h"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include "ImporterPlugin.h"
#include "PluginManager.h"
#include "util.h"
#include "metaclasses/Iterator.h"

const int ItemImporterDialog::ID_BTN_IMPORT = wxNewId();

BEGIN_EVENT_TABLE(ItemImporterDialog,wxDialog)
	EVT_BUTTON(ID_BTN_IMPORT,  ItemImporterDialog::OnImportButtonClicked)
END_EVENT_TABLE()


ItemImporterDialog::ItemImporterDialog(wxWindow * parent /* = NULL */, wxWindowID id /* = 1 */, const wxString & title /* = "Import from URL" */,
        							 const wxPoint & position /* = wxDefaultPosition */, const wxSize & size /*= wxSize(300, 300) */)
: wxDialog( parent, id, title, position, size, wxRAISED_BORDER|wxDEFAULT_DIALOG_STYLE|wxCAPTION|wxTHICK_FRAME|wxSYSTEM_MENU )
{
	wxBoxSizer *mainsizer = new wxBoxSizer(wxVERTICAL);

	// up part : some explanation + url import choice
	wxStaticBoxSizer *topSizer = new wxStaticBoxSizer(wxVERTICAL, this, _T("Import parameters"));
	wxStaticText * explain =  new wxStaticText(this, wxID_ANY, _T("Put in the field below wowhead or wow armory page of wanted item.\nURL scheme must be:\nhttp://XXX.wowhead.com/item=number (where XXX = www or any supported wowhead language) for wowhead query\nhttp://XXX.battle.net/wow/YYY/item/number (where XXX = your corresponding area (eu, us, etc.) and YYY corresponds to your locale) for wow armory query\nWhen done, click import. If everything succeed, just click on Display button to show item in viewer."));
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

	m_importedItem = 0;

}


void ItemImporterDialog::OnImportButtonClicked(wxCommandEvent &event)
{
  delete m_importedItem;
	m_importedItem = 0;
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
	  for(pluginIt.begin(); !pluginIt.ended() ; pluginIt++)
	  {
	    ImporterPlugin * plugin = *pluginIt;
	    if(plugin->acceptURL(url))
	    {
	      m_importedItem = plugin->importItem(url);
	    }
	  }
	}

	if(m_importedItem)
	{
		m_nameResult->SetLabel(CSConv(m_importedItem->name));
		m_idResult->SetLabel(wxString::Format(wxT("%i"),m_importedItem->id));
		m_displayIdResult->SetLabel(wxString::Format(wxT("%i"),m_importedItem->model));
		m_typeResult->SetLabel(wxString::Format(wxT("%i"),m_importedItem->type));
	}
	else
	{
		wxMessageDialog *dial = new wxMessageDialog(NULL, wxT("URL you entered cannot be reached. Please verify your syntax and your network connection."),
				wxT("URL Error"), wxOK | wxICON_WARNING);
		dial->ShowModal();
	}
}


ItemRecord & ItemImporterDialog::getImportedItem()
{
	ItemRecord * result = new ItemRecord();

	// @ TODO: add a copy constructor + assignment operator to ItemRecord
	if(m_importedItem)
	{
		result->id = m_importedItem->id;
		result->model = m_importedItem->model;
		result->type = m_importedItem->type;
		result->itemclass = m_importedItem->itemclass;
		result->subclass = m_importedItem->subclass;
		result->type = m_importedItem->type;
	}

	return *result;
}
