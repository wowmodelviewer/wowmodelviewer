
#ifndef IMAGECONTROL_H
#define IMAGECONTROL_H

#include <wx/wxprec.h>
#ifdef __BORLANDC__
    #pragma hdrstop
#endif
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "video.h"
#include "modelcanvas.h"
#include <wx/aui/aui.h>

class ImageControl: public wxWindow
{
	DECLARE_CLASS(ImageControl)
	DECLARE_EVENT_TABLE()
	
	wxCheckBox *lockAspect;
	wxButton *save, *cancel;
	wxTextCtrl *filename, *canvasWidth, *canvasHeight;

	wxStaticText *maxsize, *lbl1, *lbl2, *lbl3;
	
	ModelCanvas *cc;

	float aspect;
	bool locked;
	bool skipEvent;

	int x, y;
	int maxSize;

	wxAuiPaneInfo *pane;
	wxAuiManager *manager;
	
public:
	ImageControl(wxWindow *parent, wxWindowID id, ModelCanvas *cc);
	~ImageControl();

	void OnShow(wxAuiManager *m);

	void OnCheck(wxCommandEvent &event);
	void OnButton(wxCommandEvent &event);
	void OnText(wxCommandEvent &event);
};


#endif
