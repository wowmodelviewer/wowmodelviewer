#ifndef GIFEXPORTER_H
#define GIFEXPORTER_H

#include "util.h"

// wxwidgets
#include <wx/wx.h>

#include "modelcanvas.h"
#if defined(_WINDOWS) && !defined(_MINGW)
#include "AVIGenerator.h"
#endif

#include "CxImage/ximage.h" // RGBQUAD

// File Source Change log:
// Version | Date     | Comments
// -----------------------------------------------------------------------------------------------------
// 0.5.07  | 3/02/07  | Minor changes to the code to make it more standardised using hungarian notation.
//					  | Merged the "Gif" and the "Avi" exporter into one class object.
// 

// Credit goes out to the Warcraft3 Viewer
class CAnimationExporter : public wxFrame
{
	DECLARE_CLASS(CAnimationExporter)
    DECLARE_EVENT_TABLE()

private:
	size_t m_iTotalAnimFrames;			// Total frames of the model animation
	size_t m_iTotalFrames;				// Total frames of the animated gif
	size_t m_iWidth, m_iHeight;			// Width and Height of the source image
	size_t m_iNewWidth, m_iNewHeight;	// New width and height of the output image
	size_t m_iDelay;					// Delay between frames

	bool m_bTransparent, m_bDiffuse, m_bShrink, m_bGreyscale, m_bPng;	// Various options and toggles settings

	size_t m_iSize;						// Size of our data buffer to hold the pixel data

	float m_fAnimSpeed;					// Animation Speed
	ssize_t m_iTimeStep;				// frame difference between each frame
	RGBQUAD *m_pPal;

	wxString m_strFilename;				// Filename to save our animated gif into.

	//GUI objects
	wxStaticText *lblCurFrame, *lblFile, *lblTotalFrame, *lblSize, *lblDelay;
	wxButton *btnStart, *btnCancel;
	wxTextCtrl *txtFrames, *txtSizeX, *txtSizeY, *txtDelay;
	wxCheckBox *cbTrans, *cbDither, *cbShrink, *cbGrey, *cbPng;

public:
	CAnimationExporter(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxDEFAULT_FRAME_STYLE|wxCAPTION|wxFRAME_NO_TASKBAR);
	~CAnimationExporter();


	// gui functions
	// ------------------------------------------
	void OnButton(wxCommandEvent &event);
	void OnCheck(wxCommandEvent &event);


	// gif export functions
	// ------------------------------------------
	void Init(const wxString fn = wxT("temp.gif"));
	void CreateGif();


	// avi export functions
	// ------------------------------------------
	void CreateAvi(wxString fn);

	
};



#endif

