/*
 * ItemImporterDialog.h
 *
 *  Created on: 14 dec. 2013
 *
 */

#ifndef _ITEMIMPORTERDIALOG_H_
#define _ITEMIMPORTERDIALOG_H_

#include "database.h"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>


class ItemImporterDialog : public wxDialog
{
  public:
	ItemImporterDialog(wxWindow * parent = NULL, wxWindowID id = -1, const wxString & title = "Import Item from URL",
			 const wxPoint & position = wxDefaultPosition, const wxSize & size = wxSize(300, 300));

	ItemRecord & getImportedItem();

  private:
	wxTextCtrl * m_URLname;
	wxButton * m_importBtn;
	wxButton * m_displayBtn;
	wxStaticText * m_nameResult;
	wxStaticText * m_typeResult;
	wxStaticText * m_idResult;
	wxStaticText * m_displayIdResult;

	ItemRecord * m_importedItem;

	static const int ID_BTN_IMPORT;

	void OnImportButtonClicked(wxCommandEvent &event);

	DECLARE_EVENT_TABLE();

};

#endif /* _ITEMIMPORTERDIALOG_H_ */
