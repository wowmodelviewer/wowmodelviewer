#ifndef ARROWS_H
#define ARROWS_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "modelcanvas.h"


class ArrowControl: public wxWindow
{
	DECLARE_CLASS(ArrowControl)
    DECLARE_EVENT_TABLE()

	wxComboBox *joint, *model; //, *tex;
	wxButton *attach, *exit, *clear;
	wxSlider *rot, *position, *scale;

	std::vector<Attachment*> atts;
	Attachment *curAtt;
	Attachment *charAtt;

public:

	ArrowControl(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, Attachment *att = NULL);
	~ArrowControl();

	void OnButton(wxCommandEvent &event);
	void OnCombo(wxCommandEvent &event);
	void OnSlider(wxCommandEvent &event);
	void OnClose(wxCloseEvent &event);
};


#endif // ARROWS_H

