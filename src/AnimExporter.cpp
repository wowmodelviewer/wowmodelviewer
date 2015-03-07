
#include "../../../AnimExporter.h"

#include "globalvars.h"
#include "Quantize.h"

#include "logger/Logger.h"

// CxImage
#include "CxImage/ximage.h"
#include "CxImage/ximagif.h"
#include "CxImage/ximabmp.h"
#include "next-gen/games/wow/Attachment.h"

IMPLEMENT_CLASS(CAnimationExporter, wxFrame)

BEGIN_EVENT_TABLE(CAnimationExporter, wxFrame)
	EVT_BUTTON(ID_GIFSTART, CAnimationExporter::OnButton)
	EVT_BUTTON(ID_GIFEXIT,	CAnimationExporter::OnButton)

	EVT_CHECKBOX(ID_GIFTRANSPARENT, CAnimationExporter::OnCheck)
	EVT_CHECKBOX(ID_GIFDIFFUSE,		CAnimationExporter::OnCheck)
	EVT_CHECKBOX(ID_GIFSHRINK,		CAnimationExporter::OnCheck)
	EVT_CHECKBOX(ID_GIFGREYSCALE,	CAnimationExporter::OnCheck)
	EVT_CHECKBOX(ID_PNGSEQ,	CAnimationExporter::OnCheck)
END_EVENT_TABLE()

// This creates our frame and all our objects
CAnimationExporter::CAnimationExporter(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
{
	if (!g_canvas)
		return;	

	if (Create(parent, id, title, pos, size, style|wxTAB_TRAVERSAL, wxT("GifExporterFrame")) == false) {
		wxMessageBox(wxT("Failed to create the Gif Exporter window!"), wxT("Error"));
		wxLogMessage(wxT("GUI Error: Failed to create the Gif Exporter window!"));
		this->Destroy();
		return;
	}
	
	lblFile = new wxStaticText(this, wxID_ANY, wxEmptyString, wxPoint(10,5), wxSize(320,20));
	lblCurFrame = new wxStaticText(this, wxID_ANY, wxT("Current Frame: 0"), wxPoint(10,25), wxSize(100,20));
	
	lblTotalFrame = new wxStaticText(this, wxID_ANY, wxT("Total Frames:"), wxPoint(10,45), wxDefaultSize);
	txtFrames = new wxTextCtrl(this, ID_GIFTOTALFRAME, wxEmptyString, wxPoint(90,45), wxSize(30,18));
	
	cbTrans = new wxCheckBox(this, ID_GIFTRANSPARENT, wxT("Transparency"), wxPoint(10,65), wxDefaultSize, 0);
	cbGrey = new wxCheckBox(this, ID_GIFGREYSCALE, wxT("Greyscale"), wxPoint(130,65), wxDefaultSize, 0);
	cbPng = new wxCheckBox(this, ID_PNGSEQ, wxT("PNG Sequence"), wxPoint(250,65), wxDefaultSize, 0);
	cbDither = new wxCheckBox(this, ID_GIFDIFFUSE, wxT("Error Diffusion"), wxPoint(10,85), wxDefaultSize, 0);
	cbShrink = new wxCheckBox(this, ID_GIFSHRINK, wxT("Resize"), wxPoint(130,85), wxDefaultSize, 0);

	lblSize = new wxStaticText(this, wxID_ANY, wxT("Size Dimensions:"), wxPoint(10,105), wxDefaultSize);
	txtSizeX = new wxTextCtrl(this, wxID_ANY, wxT("0"), wxPoint(100,105), wxSize(40,18));
	txtSizeX->Enable(false);
	txtSizeY = new wxTextCtrl(this, wxID_ANY, wxT("0"), wxPoint(150,105), wxSize(40,18));
	txtSizeY->Enable(false);

	lblDelay = new wxStaticText(this, wxID_ANY, wxT("Gif Frame Delay: (1-100)"), wxPoint(10,128), wxDefaultSize);
	txtDelay = new wxTextCtrl(this, wxID_ANY, wxT("5"), wxPoint(140,125), wxSize(30,18));
	
	btnStart = new wxButton(this, ID_GIFSTART, wxT("Start"), wxPoint(10,155), wxSize(62,26));
	btnCancel = new wxButton(this, ID_GIFEXIT, wxT("Cancel"), wxPoint(80,155), wxSize(62,26));
}

void CAnimationExporter::Init(const wxString fn)
{
	if (!g_canvas)
		return;

	m_pPal = NULL;
	m_fAnimSpeed = 0.0f;

	m_bTransparent = false;
	m_bDiffuse = false;
	m_bShrink = false;
	m_bGreyscale = false;
	m_bPng = false;
	
	m_iNewWidth = 0;
	m_iNewHeight = 0;

	m_iTotalAnimFrames = g_canvas->model->animManager->GetFrameCount();
	m_strFilename = fn;

	lblFile->SetLabel(fn);

	size_t i = (m_iTotalAnimFrames / 50);
	txtFrames->SetLabel(wxEmptyString);
	*txtFrames << (int)i;

	btnStart->Enable(true);
	btnCancel->Enable(true);
	cbGrey->Enable(true);
	cbPng->Enable(true);
	cbTrans->Enable(true);
	cbDither->Enable(true);
	cbShrink->Enable(true);
	txtFrames->Enable(true);
	txtSizeX->Enable(true);
	txtSizeY->Enable(true);
	txtDelay->Enable(true);

	txtSizeX->SetValue(wxT("0"));
	txtSizeY->SetValue(wxT("0"));
	cbShrink->SetValue(false);
}

CAnimationExporter::~CAnimationExporter()
{
	//canvas = NULL;
	//this = NULL;
}


// This must be called before any frame-saving is attempted.
void CAnimationExporter::CreateGif()
{
	if (!g_canvas || !g_canvas->model || !g_canvas->model->animManager) {
		wxMessageBox(wxT("Unable to create animated GIF!"), wxT("Error"));
		wxLogMessage(wxT("Error: Unable to created animated GIF.  A required objects pointer was null!"));
		Show(false);
		return;
	}

	CxImage **gifImages = NULL;		// Our pointer array of images

	// Reset the state of our GUI objects
	btnStart->Enable(false);
	btnCancel->Enable(false);
	cbGrey->Enable(false);
	cbPng->Enable(false);
	cbTrans->Enable(false);
	cbDither->Enable(false);
	cbShrink->Enable(false);
	txtFrames->Enable(false);
	txtSizeX->Enable(false);
	txtSizeY->Enable(false);
	txtDelay->Enable(false);
	// Pause our rendering to screen so we can focus on making the animated image
	video.render = false;
	
	m_fAnimSpeed = g_canvas->model->animManager->GetSpeed(); // Save the old animation speed
	g_canvas->model->animManager->SetSpeed(1.0f);	// Set it to the normal speed.

	m_iTotalAnimFrames = g_canvas->model->animManager->GetFrameCount();
	wxString(txtFrames->GetValue()).ToLong((long*)&m_iTotalFrames);
	wxString(txtDelay->GetValue()).ToLong((long*)&m_iDelay);

	// will crash program - prevent this from happening
	if (m_iTotalFrames > m_iTotalAnimFrames) {
		wxMessageBox(wxT("Impossible to make a gif with more frames than the model animation.\nClosing gif exporter."), wxT("Error"));
		wxLogMessage(wxT("Error: Unable to make a gif with more frames than the model animation."));
		this->Show(false);
		return;
	}

	if (m_iDelay < 1)
		m_iDelay = 1;
	if (m_iDelay > 100)
		m_iDelay = 100;

	m_iTimeStep = int(m_iTotalAnimFrames / m_iTotalFrames);	// Total number of frames in the animation / total frames going into our exported animation image

	if (m_bShrink) {
		wxString(txtSizeX->GetValue()).ToLong((long*)&m_iNewWidth);
		wxString(txtSizeY->GetValue()).ToLong((long*)&m_iNewHeight);

		// Just a minor check,  final image size can not be smaller than 32x32 pixels.
		if (m_iNewWidth < 32 || m_iNewHeight < 32) {
			m_iNewWidth = 32;
			m_iNewHeight = 32;
		}
	}

#ifdef _WINDOWS
	// CREATE OUR RENDERTOTEXTURE OBJECT
	// -------------------------------------------
	// if either are supported use our 'RenderTexture' object.
	if (video.supportPBO || video.supportVBO) { 
		g_canvas->rt = new RenderTexture();

		if (!g_canvas->rt) {
			wxLogMessage(wxT("Error: RenderToTexture object is null!"));
			this->Show(false);
			return;
		}

		g_canvas->rt->Init((HWND)g_canvas->GetHandle(), 0, 0, video.supportFBO);
		
		m_iWidth = g_canvas->rt->nWidth;
		m_iHeight = g_canvas->rt->nHeight;
		g_canvas->rt->BeginRender();
	} else 
#endif
	{
		glReadBuffer(GL_BACK);
		int screenSize[4];
		glGetIntegerv(GL_VIEWPORT, (GLint*)screenSize);				// get the width/height of the canvas
		m_iWidth = screenSize[2];
		m_iHeight = screenSize[3];
		return;
	}
	
	// Stop our animation
	g_canvas->model->animManager->Pause(true);
	g_canvas->model->animManager->Stop();
	g_canvas->model->animManager->AnimateParticles();

	// Size of our buffer to hold the pixel data
	m_iSize = m_iWidth*m_iHeight*4;	// (width*height*bytesPerPixel)	

	// Create one frame to make our optimal colour palette from.
	unsigned char *buffer = new unsigned char[m_iSize];
	gifImages = new CxImage*[m_iTotalFrames];

	for(unsigned int i=0; i<m_iTotalFrames && !m_bPng; i++) {
		lblCurFrame->SetLabel(wxString::Format(wxT("Current Frame: %i"), i));

		this->Refresh();
		this->Update();

		CxImage *newImage = new CxImage(0);

		g_canvas->RenderToBuffer();

		glReadPixels(0, 0, (GLsizei)m_iWidth, (GLsizei)m_iHeight, GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer);
		newImage->CreateFromArray(buffer, (DWORD)m_iWidth, (DWORD)m_iHeight, 32, (DWORD)(m_iWidth*4), false);

		// not needed due to the code just below, which fixes the issue with particles
		//g_canvas->model->animManager->SetTimeDiff(m_iTimeStep);
		//g_canvas->model->animManager->Tick(m_iTimeStep);

		if (g_canvas->root)
			g_canvas->root->tick((float)m_iTimeStep);
		if (g_canvas->sky)
			g_canvas->sky->tick((float)m_iTimeStep);
		

		#ifdef _WINDOWS
		if (m_bGreyscale)
			newImage->GrayScale();
		#endif //_WINDOWS

		if(m_bShrink && m_iNewWidth!=m_iWidth && m_iNewHeight!=m_iHeight)
			newImage->Resample((long)m_iNewWidth, (long)m_iNewHeight, 2);

		// if (Optimise) {
		if (!m_pPal) {
			CQuantizer q(256, 8);
			q.ProcessImage((HANDLE)newImage->GetDIB());
			m_pPal = (RGBQUAD*) calloc(256*sizeof(RGBQUAD), 1); //This creates our gifs optimised global colour palette
			q.SetColorTable(m_pPal);
		}

		newImage->DecreaseBpp(8, m_bDiffuse, m_pPal, 256);
		newImage->SetCodecOption(2); // for LZW compression

		if(m_bTransparent)
			newImage->SetTransIndex(newImage->GetPixelIndex(0,0));

		newImage->SetFrameDelay((DWORD)m_iDelay);
		
		gifImages[i] = newImage;
		
		// All the memory that we allocate for newImage gets cleared at the end
	}

	//PNG Sequence Exporter, use if Checkbox is true
	for(unsigned int i=0; i<m_iTotalFrames && m_bPng; i++) {
		lblCurFrame->SetLabel(wxString::Format(wxT("Current Frame: %i"), i));

		this->Refresh();
		this->Update();

		CxImage *newImage = new CxImage(0);
		CxImage *newImage2 = new CxImage(0);
		
		g_canvas->RenderToBuffer();
		wxString stat;

		glReadPixels(0, 0, (GLsizei)m_iWidth, (GLsizei)m_iHeight, GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer);

		newImage->CreateFromArray(buffer, (DWORD)m_iWidth, (DWORD)m_iHeight, 32, (DWORD)(m_iWidth*4), false);

		/*
		 *Because Alpha Channel textures are a bit messed up in the OpenGL renders,
		 *alpha channels will have to be 1-bit to "hide" any texture errors
		 */
		if (m_bTransparent){
			newImage->AlphaSplit(newImage2); //split alpha to another cximage object
			newImage2->Threshold(1); //convert 8bit alpha to 1bit
			//newImage2->GrayScale(); //prepare conversion for application to alpha channel
			newImage->AlphaSet(*newImage2); //apply mock 1-bit alpha channel
		}else{
			newImage->AlphaCreate();
			newImage->IncreaseBpp(32);
		}

		// not needed due to the code just below, which fixes the issue with particles
		//g_canvas->model->animManager->SetTimeDiff(m_iTimeStep);
		//g_canvas->model->animManager->Tick(m_iTimeStep);
		if (g_canvas->root)
			g_canvas->root->tick((float)m_iTimeStep);
		if (g_canvas->sky)
			g_canvas->sky->tick((float)m_iTimeStep);
		

		#ifdef _WINDOWS
		if (m_bGreyscale)
			newImage->GrayScale();
		#endif //_WINDOWS

		// if (Optimise) {

		// Append PNG extension, save out PNG file with frame number
		wxString filen = m_strFilename;
		filen << wxT("_") << i << wxT(".png");
		newImage->Save(filen.mb_str(), CXIMAGE_FORMAT_PNG);
		
		//gifImages must not be empty
		gifImages[i] = newImage;
		// All the memory that we allocate for newImage gets cleared at the end
	}
	wxDELETEA(buffer);

#ifdef _WINDOWS
	if (video.supportPBO || video.supportVBO) {
		g_canvas->rt->EndRender();

		// Clear RenderTexture object.
		g_canvas->rt->Shutdown();
		wxDELETE(g_canvas->rt);
	}
#endif
	if(!m_bPng){
	// CREATE THE ACTUAL MULTI-IMAGE GIF ANIMATION
	// ------------------------------------------------------
	// Create the file and write all the data
	// Open/Create the file that were going to save to

	// Append GIF extension
	wxString filen = m_strFilename;
	filen << wxT(".gif");

	FILE *hFile = NULL;
#if	defined(_WINDOWS) && !defined(_MINGW)
	fopen_s(&hFile, filen.mb_str(), "wb");
#else
	hFile = fopen(filen.mb_str(), "wb");
#endif

	// Set gif options
	CxImageGIF multiImage;
	multiImage.SetComment("Exported from WoW Model Viewer");
	if (m_bTransparent)
		multiImage.SetDisposalMethod(2);
	else
		multiImage.SetDisposalMethod(0);
	
	multiImage.SetFrameDelay((DWORD)m_iDelay);
	multiImage.SetCodecOption(2); // LZW
	multiImage.SetLoops(0);		// Set the animation to loop indefinately.

	// Create/Compose the animated gif
	multiImage.Encode(hFile, gifImages, (int)m_iTotalFrames, false);

	// ALL DONE, START THE CLEAN UP
	// --------------------------------------------------------
	// Close file
	fclose(hFile);
	}

	// Free the memory used by all the images to create the GIF
	for(unsigned int i=0; i<m_iTotalFrames; i++) {
		gifImages[i]->Destroy();
		wxDELETE(gifImages[i]);
	}
	wxDELETEA(gifImages);

	// Free memory used by the colour palette
	if (m_pPal) {
		free(m_pPal);
		m_pPal = NULL;
	}

	wxLogMessage(wxT("Info: GIF Animation successfully created."));

	g_canvas->model->animManager->SetSpeed(m_fAnimSpeed); // Return the animation speed back to whatever it was previously set as
	g_canvas->model->animManager->Play();

	Show(false);

	video.render = true;
	g_canvas->InitView();
}


void CAnimationExporter::OnButton(wxCommandEvent &event)
{
	if (event.GetId() == ID_GIFSTART){
		CreateGif();
	} else if (event.GetId() == ID_GIFEXIT) {
		Show(false);
	}
}

void CAnimationExporter::OnCheck(wxCommandEvent &event)
{
	if (event.GetId() == ID_GIFTRANSPARENT) {
		m_bTransparent = event.IsChecked();
	} else if (event.GetId() == ID_GIFDIFFUSE) {
		m_bDiffuse = event.IsChecked();
	} else if (event.GetId() == ID_GIFSHRINK) {
		m_bShrink = event.IsChecked();
		txtSizeX->Enable(m_bShrink);
		txtSizeY->Enable(m_bShrink);
		if (m_bShrink) {
			int screenSize[4];
			glGetIntegerv(GL_VIEWPORT, (GLint*)screenSize);				// get the width/height of the canvas
			txtSizeX->Clear();
			*txtSizeX << screenSize[2];
			txtSizeY->Clear();
			*txtSizeY << screenSize[3];
		}
		
	} else if (event.GetId() == ID_GIFGREYSCALE) {
		m_bGreyscale = event.IsChecked();
	}
	else if (event.GetId() == ID_PNGSEQ) {
		m_bPng = event.IsChecked();
	}
}

#if defined(_WINDOWS) && !defined(_MINGW)
void CAnimationExporter::CreateAvi(wxString fn)
{

	if (!g_canvas || !g_canvas->model || !g_canvas->model->animManager) {
		wxMessageBox(wxT("Unable to create AVI animation!"), wxT("Error"));
		wxLogMessage(wxT("Error: Unable to created AVI animation.  A required object pointer was null!"));
		return;
	}

	// Pause rendering to canvas
	video.render = false;

	// Save the old animation speed and set back to default
	m_fAnimSpeed = g_canvas->model->animManager->GetSpeed(); 
	g_canvas->model->animManager->SetSpeed(1.0f);	// Set it to the normal speed.

	m_iTotalAnimFrames = g_canvas->model->animManager->GetFrameCount();
	m_iTotalFrames = (m_iTotalAnimFrames / 25);

	if (video.supportPBO || video.supportVBO) { // if either are supported use our 'RenderTexture' object.
		g_canvas->rt = new RenderTexture();

		if (!g_canvas->rt) {
			wxLogMessage(wxT("Error: RenderToTexture object is null!"));
			Show(false);
			return;
		}

		g_canvas->rt->Init((HWND)g_canvas->GetHandle(), 512, 512, video.supportFBO);
		
		m_iWidth = g_canvas->rt->nWidth;
		m_iHeight = g_canvas->rt->nHeight;

		//canvas->rt->BeginRender();
		//canvas->RenderToBuffer();
		//rt->BindTexture(); 
	} else {
		glReadBuffer(GL_BACK);
		int screenSize[4];
		glGetIntegerv(GL_VIEWPORT, screenSize);				// get the width/height of the canvas
		m_iWidth = screenSize[2];
		m_iHeight = screenSize[3];
	}

	// will crash program - prevent this from happening
	if (m_iTotalFrames > m_iTotalAnimFrames) {
		wxMessageBox(wxT("Impossible to make a gif with more frames than the model animation.\nClosing gif exporter."), wxT("Error"));
		wxLogMessage(wxT("Error: Unable to make a gif with more frames than the model animation."));
		return;
	}

	const ssize_t timeStep = (m_iTotalAnimFrames / m_iTotalFrames);
	const ssize_t bufSize = m_iWidth*m_iHeight*3;	// (width*height*bytesPerPixel - only 3 for RGB, no alpha)	

	CAVIGenerator AviGen;

	BITMAPINFOHEADER bmHeader;
	bmHeader.biWidth = (LONG)m_iWidth;
	bmHeader.biHeight = (LONG)m_iHeight;
	bmHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmHeader.biPlanes = 1;
	bmHeader.biBitCount = 24;
	bmHeader.biSizeImage = bufSize; 
	bmHeader.biCompression = BI_RGB;		//BI_RGB means BRG in reality
	bmHeader.biClrUsed = 0;
	bmHeader.biClrImportant = 0;
	bmHeader.biXPelsPerMeter = 0;
	bmHeader.biYPelsPerMeter = 0;
	
	// set our avi config
	AviGen.SetRate(25);						// set 25fps
	AviGen.SetBitmapHeader(bmHeader);
	AviGen.SetFileName(fn.fn_str());

	AviGen.InitEngineForWrite((HWND)this->GetParent()->GetHandle());
	
	// Stop our animation
	g_canvas->model->animManager->Pause(true);
	g_canvas->model->animManager->Stop();
	g_canvas->model->animManager->AnimateParticles();

	// Create one frame to make our optimal colour palette from.
	unsigned char *buffer = new unsigned char[bufSize];

	// Iterate through the frames saving the image to a buffer then writing it to the AVI
	for(unsigned int i=0; i<m_iTotalFrames; i++) {
		g_canvas->RenderToBuffer();
		glReadPixels(0, 0, (GLsizei)m_iWidth, (GLsizei)m_iHeight, GL_BGR_EXT, GL_UNSIGNED_BYTE, buffer);
		AviGen.AddFrame(buffer);

		// not needed due to the code just below
		//g_canvas->model->animManager->SetTimeDiff(timeStep);
		//g_canvas->model->animManager->Tick(timeStep);

		// Animate particles
		if (g_canvas->root)
			g_canvas->root->tick((float)m_iTimeStep);
		if (g_canvas->sky)
			g_canvas->sky->tick((float)m_iTimeStep);
	}

	// Release our Avi writing object
	AviGen.ReleaseEngine();

	// Clear our pixel data buffer.
	wxDELETEA(buffer);

	// Clear the Render Texture object from memory if it exists.
	if (g_canvas->rt) {
		g_canvas->rt->EndRender();

		// Clear RenderTexture object.
		g_canvas->rt->Shutdown();
		wxDELETE(g_canvas->rt);
	}

	g_canvas->model->animManager->SetSpeed(m_fAnimSpeed); // Return the animation speed back to whatever it was previously set as
	g_canvas->model->animManager->Play();
	video.render = true;
	g_canvas->InitView();
}
#else
void CAnimationExporter::CreateAvi(wxString)
{
}
#endif

// --

