/*
 * NPCimporterDialog.h
 *
 *  Created on: 5 nov. 2013
 *
 */

#ifndef _NPCIMPORTERDIALOG_H_
#define _NPCIMPORTERDIALOG_H_

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>

class NPCimporterDialog : public wxDialog
{
  public:
	NPCimporterDialog(wxWindow * parent = NULL, wxWindowID id = -1, const wxString & title = "Import from URL",
			 const wxPoint & position = wxDefaultPosition, const wxSize & size = wxSize(300, 300));

	int getImportedId();
	wxString getNPCLine();

  private:
	wxTextCtrl * m_URLname;
	wxButton * m_importBtn;
	wxButton * m_displayBtn;
	wxStaticText * m_nameResult;
	wxStaticText * m_typeResult;
	wxStaticText * m_idResult;
	wxStaticText * m_displayIdResult;

	static const int ID_BTN_IMPORT;

	void OnImportButtonClicked(wxCommandEvent &event);
	void OnDisplayButtonClicked(wxCommandEvent &event);

	DECLARE_EVENT_TABLE();

};

#endif /* _NPCIMPORTERDIALOG_H_ */
