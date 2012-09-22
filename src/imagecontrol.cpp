
#include "imagecontrol.h"
#include "enums.h"

IMPLEMENT_CLASS(ImageControl, wxWindow)

BEGIN_EVENT_TABLE(ImageControl, wxWindow)

	EVT_TEXT(ID_IMAGE_CANVASWIDTH, ImageControl::OnText)
	EVT_TEXT(ID_IMAGE_CANVASHEIGHT, ImageControl::OnText) 

	EVT_CHECKBOX(ID_IMAGE_LOCKASPECT, ImageControl::OnCheck)

	EVT_BUTTON(ID_IMAGE_SAVE, ImageControl::OnButton)
	EVT_BUTTON(ID_IMAGE_CANCEL, ImageControl::OnButton)
END_EVENT_TABLE()

ImageControl::ImageControl(wxWindow *parent, wxWindowID id, ModelCanvas *cc)
{
	aspect = 0.0f;
	this->cc = cc;

	if (Create(parent, id, wxDefaultPosition, wxSize(300,150), wxTAB_TRAVERSAL, wxT("ImageControlFrame")) == false) {
		wxLogMessage(wxT("Serious Error: Failed to create a window for our ImageControl!"));
		return;
	}

	lbl1 = new wxStaticText(this, wxID_ANY, wxT("Filename:"), wxPoint(10,10), wxDefaultSize, wxALIGN_LEFT);
	filename = new wxTextCtrl(this, ID_IMAGE_FILENAME, wxEmptyString, wxPoint(60,5), wxSize(200,20), wxTE_READONLY);

	lbl2 = new wxStaticText(this, wxID_ANY, wxT("Canvas Width:"), wxPoint(10,40), wxDefaultSize, wxALIGN_LEFT);
	canvasWidth = new wxTextCtrl(this, ID_IMAGE_CANVASWIDTH, wxEmptyString, wxPoint(90,40), wxSize(40,20), 0);
	lbl3 = new wxStaticText(this, wxID_ANY, wxT("Canvas Height:"), wxPoint(10,65), wxDefaultSize, wxALIGN_LEFT);
	canvasHeight = new wxTextCtrl(this, ID_IMAGE_CANVASHEIGHT, wxEmptyString, wxPoint(90,65), wxSize(40,20), 0);
	
	lockAspect = new wxCheckBox(this, ID_IMAGE_LOCKASPECT, wxT("Lock Aspect"), wxPoint(140,45), wxDefaultSize);

	maxsize = new wxStaticText(this, wxID_ANY, wxT("Max Size: "), wxPoint(140,70), wxDefaultSize, 0);

	save = new wxButton(this, ID_IMAGE_SAVE, wxT("Save"), wxPoint(100,95), wxDefaultSize, 0);
	cancel = new wxButton(this, ID_IMAGE_CANCEL, wxT("Cancel"), wxPoint(180,95), wxDefaultSize, 0);
}


ImageControl::~ImageControl()
{


}

void ImageControl::OnShow(wxAuiManager *m)
{
	manager = m;
	Show(true);
	manager->GetPane(this).Show(true);
	manager->Update();
	skipEvent = false;
	//locked = false;
	lockAspect->SetValue(locked);

	int screenSize[4];
	glGetIntegerv(GL_VIEWPORT, (GLint*)screenSize);

	x = screenSize[2];
	y = screenSize[3];

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint*)&maxSize); 

	maxsize->SetLabel(wxString::Format(wxT("Max Size: %i"), maxSize));

	aspect = ((float)screenSize[2] / (float)screenSize[3]);

	wxString tmp = wxT("screenshot_");
	tmp << ssCounter;
	wxFileDialog dialog(this, wxT("Save screenshot"), wxEmptyString, tmp, wxT("Bitmap Images (*.bmp)|*.bmp|TGA Images (*.tga)|*.tga|JPEG Images (*.jpg)|*.jpg|PNG Images (*.png)|*.png"), wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
	dialog.SetFilterIndex(imgFormat);

	if (dialog.ShowModal()==wxID_OK) {
		imgFormat = dialog.GetFilterIndex();
		tmp = dialog.GetPath();
		filename->SetValue(tmp);
	}

	tmp = wxEmptyString;
	tmp << screenSize[2];
	canvasWidth->SetValue(tmp);
	tmp = wxEmptyString;
	tmp << screenSize[3];
	canvasHeight->SetValue(tmp);
}

void ImageControl::OnCheck(wxCommandEvent &event)
{
	locked = event.IsChecked();
}

void ImageControl::OnButton(wxCommandEvent &event)
{
	if (event.GetId() == ID_IMAGE_SAVE) {
		video.render = false;
		manager->GetPane(this).Show(false);
		manager->Update();
		cc->Screenshot(filename->GetValue(), x ,y);
		ssCounter++;
		cc->InitView();
		video.render = true;

	} else if (event.GetId() == ID_IMAGE_CANCEL) {
		//Show(false);
		manager->GetPane(this).Show(false);
		manager->Update();
	}
}

void ImageControl::OnText(wxCommandEvent &event)
{
	if (!locked)
		return;

	if (skipEvent) {
		skipEvent = false;
		return;
	}
	skipEvent = true;

	long value=0;
	wxString tmp;

	if (event.GetId() == ID_IMAGE_CANVASWIDTH) {
		tmp = canvasWidth->GetValue();
		tmp.ToLong(&value);

		x = value;
		value = int(value / aspect);
		y = value;
		canvasHeight->SetValue(wxString::Format(wxT("%i"), value));
	} else if (event.GetId() == ID_IMAGE_CANVASHEIGHT) {
		tmp = canvasHeight->GetValue();
		tmp.ToLong(&value);

		y = value;
		value = int(value * aspect);
		x = value;
		canvasWidth->SetValue(wxString::Format(wxT("%i"), value));
	}

	if (x > maxSize) {
		x = maxSize;
		canvasWidth->SetValue(wxString::Format(wxT("%i"), x));
	}
	if (y > maxSize) {
		y = maxSize;
		canvasHeight->SetValue(wxString::Format(wxT("%i"), x));
	}
}
