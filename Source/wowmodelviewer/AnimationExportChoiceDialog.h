#pragma once

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/choicdlg.h>

class wxButton;

class AnimationExportChoiceDialog : public wxMultiChoiceDialog
{
public:
	AnimationExportChoiceDialog(wxWindow* parent, const wxString& message, const wxString& caption,
	                            const wxArrayString& choices);
	~AnimationExportChoiceDialog() = default;

private:
	void updateButtons(wxCommandEvent& event);
	void OnSelectAll(wxCommandEvent& event);
	void OnUnselectAll(wxCommandEvent& event);

	wxButton* m_selectall;
	wxButton* m_unselectall;

	DECLARE_EVENT_TABLE();
};
