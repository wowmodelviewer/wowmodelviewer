#include "AnimExporter.h"
#include <memory>
#include <QtGui/QImage>
#include "Attachment.h"
#include "enums.h"
#include "globalvars.h"
#include "logger/Logger.h"

IMPLEMENT_CLASS(CAnimationExporter, wxFrame)

BEGIN_EVENT_TABLE(CAnimationExporter, wxFrame)
	EVT_BUTTON(ID_GIFSTART, CAnimationExporter::OnButton)
	EVT_BUTTON(ID_GIFEXIT, CAnimationExporter::OnButton)

	EVT_CHECKBOX(ID_GIFTRANSPARENT, CAnimationExporter::OnCheck)
	EVT_CHECKBOX(ID_GIFDIFFUSE, CAnimationExporter::OnCheck)
	EVT_CHECKBOX(ID_GIFSHRINK, CAnimationExporter::OnCheck)
	EVT_CHECKBOX(ID_GIFGREYSCALE, CAnimationExporter::OnCheck)
	EVT_CHECKBOX(ID_PNGSEQ, CAnimationExporter::OnCheck)
END_EVENT_TABLE()

// This creates our frame and all our objects
CAnimationExporter::CAnimationExporter(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos,
                                       const wxSize& size, long style)
{
	if (!g_canvas)
		return;

	if (Create(parent, id, title, pos, size, style | wxTAB_TRAVERSAL, wxT("GifExporterFrame")) == false)
	{
		wxMessageBox(wxT("Failed to create the Gif Exporter window!"), wxT("Error"));
		LOG_ERROR << "Failed to create the Gif Exporter window!";
		this->wxTopLevelWindowBase::Destroy();
		return;
	}

	lblFile = new wxStaticText(this, wxID_ANY, wxEmptyString, wxPoint(10, 5), wxSize(320, 20));
	lblCurFrame = new wxStaticText(this, wxID_ANY, wxT("Current Frame: 0"), wxPoint(10, 25), wxSize(100, 20));

	lblTotalFrame = new wxStaticText(this, wxID_ANY, wxT("Total Frames:"), wxPoint(10, 45), wxDefaultSize);
	txtFrames = new wxTextCtrl(this, ID_GIFTOTALFRAME, wxEmptyString, wxPoint(90, 45), wxSize(30, 18));

	cbTrans = new wxCheckBox(this, ID_GIFTRANSPARENT, wxT("Transparency"), wxPoint(10, 65), wxDefaultSize, 0);
	cbGrey = new wxCheckBox(this, ID_GIFGREYSCALE, wxT("Greyscale"), wxPoint(130, 65), wxDefaultSize, 0);
	cbPng = new wxCheckBox(this, ID_PNGSEQ, wxT("PNG Sequence"), wxPoint(250, 65), wxDefaultSize, 0);
	cbDither = new wxCheckBox(this, ID_GIFDIFFUSE, wxT("Error Diffusion"), wxPoint(10, 85), wxDefaultSize, 0);
	cbShrink = new wxCheckBox(this, ID_GIFSHRINK, wxT("Resize"), wxPoint(130, 85), wxDefaultSize, 0);

	lblSize = new wxStaticText(this, wxID_ANY, wxT("Size Dimensions:"), wxPoint(10, 105), wxDefaultSize);
	txtSizeX = new wxTextCtrl(this, wxID_ANY, wxT("0"), wxPoint(100, 105), wxSize(40, 18));
	txtSizeX->Enable(false);
	txtSizeY = new wxTextCtrl(this, wxID_ANY, wxT("0"), wxPoint(150, 105), wxSize(40, 18));
	txtSizeY->Enable(false);

	lblDelay = new wxStaticText(this, wxID_ANY, wxT("Gif Frame Delay: (1-100)"), wxPoint(10, 128), wxDefaultSize);
	txtDelay = new wxTextCtrl(this, wxID_ANY, wxT("5"), wxPoint(140, 125), wxSize(30, 18));

	btnStart = new wxButton(this, ID_GIFSTART, wxT("Start"), wxPoint(10, 155), wxSize(62, 26));
	btnCancel = new wxButton(this, ID_GIFEXIT, wxT("Cancel"), wxPoint(80, 155), wxSize(62, 26));
}

void CAnimationExporter::Init(const wxString fn)
{
	if (!g_canvas)
		return;

	m_fAnimSpeed = 0.0f;

	m_bTransparent = false;
	m_bDiffuse = false;
	m_bShrink = false;
	m_bGreyscale = false;
	m_bPng = false;

	m_iNewWidth = 0;
	m_iNewHeight = 0;

	m_iTotalAnimFrames = g_canvas->model()->animManager->GetFrameCount();
	m_strFilename = fn;

	lblFile->SetLabel(fn);

	const size_t i = (m_iTotalAnimFrames / 50);
	txtFrames->SetLabel(wxEmptyString);
	*txtFrames << static_cast<int>(i);

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

CAnimationExporter::~CAnimationExporter() = default;
/*{
	//canvas = NULL;
	//this = NULL;
}*/

// This must be called before any frame-saving is attempted.
void CAnimationExporter::CreateGif()
{
	if (!g_canvas || !g_canvas->model() || !g_canvas->model()->animManager)
	{
		wxMessageBox(wxT("Unable to export animation!"), wxT("Error"));
		LOG_ERROR << "Unable to export animation. A required object pointer was null!";
		Show(false);
		return;
	}

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

	m_fAnimSpeed = g_canvas->model()->animManager->GetSpeed(); // Save the old animation speed
	g_canvas->model()->animManager->SetSpeed(1.0f); // Set it to the normal speed.

	m_iTotalAnimFrames = g_canvas->model()->animManager->GetFrameCount();
	wxString(txtFrames->GetValue()).ToLong(reinterpret_cast<long*>(&m_iTotalFrames));
	wxString(txtDelay->GetValue()).ToLong(reinterpret_cast<long*>(&m_iDelay));

	// will crash program - prevent this from happening
	if (m_iTotalFrames > m_iTotalAnimFrames)
	{
		wxMessageBox(
			wxT("Impossible to export with more frames than the model animation.\nClosing exporter."),
			wxT("Error"));
		LOG_ERROR << "Unable to export with more frames than the model animation.";
		this->Show(false);
		return;
	}

	if (m_iDelay < 1)
		m_iDelay = 1;
	if (m_iDelay > 100)
		m_iDelay = 100;

	m_iTimeStep = static_cast<int>(m_iTotalAnimFrames / m_iTotalFrames);
	// Total number of frames in the animation / total frames going into our exported animation image

	if (m_bShrink)
	{
		wxString(txtSizeX->GetValue()).ToLong(reinterpret_cast<long*>(&m_iNewWidth));
		wxString(txtSizeY->GetValue()).ToLong(reinterpret_cast<long*>(&m_iNewHeight));

		// Just a minor check, final image size can not be smaller than 32x32 pixels.
		if (m_iNewWidth < 32 || m_iNewHeight < 32)
		{
			m_iNewWidth = 32;
			m_iNewHeight = 32;
		}
	}

	// CREATE OUR RENDERTOTEXTURE OBJECT
	// -------------------------------------------
	// if either are supported use our 'RenderTexture' object.
	if (video.supportPBO || video.supportVBO)
	{
		g_canvas->rt = new RenderTexture();
		g_canvas->rt->Init(0, 0, video.supportFBO);

		m_iWidth = g_canvas->rt->nWidth;
		m_iHeight = g_canvas->rt->nHeight;
		g_canvas->rt->BeginRender();
	}
	else
	{
		glReadBuffer(GL_BACK);
		const int screenSize[4]{};
		glGetIntegerv(GL_VIEWPORT, const_cast<GLint*>(screenSize)); // get the width/height of the canvas
		m_iWidth = screenSize[2];
		m_iHeight = screenSize[3];
		return;
	}

	// Stop our animation
	g_canvas->model()->animManager->Pause(true);
	g_canvas->model()->animManager->Stop();

	// Size of our buffer to hold the pixel data
	m_iSize = m_iWidth * m_iHeight * 4; // (width*height*bytesPerPixel)

	auto buffer = std::make_unique<unsigned char[]>(m_iSize);

	// PNG Sequence Export
	for (unsigned int i = 0; i < m_iTotalFrames; i++)
	{
		lblCurFrame->SetLabel(wxString::Format(wxT("Current Frame: %i"), i));

		this->Refresh();
		this->Update();

		g_canvas->RenderToBuffer();

		glReadPixels(0, 0, static_cast<GLsizei>(m_iWidth), static_cast<GLsizei>(m_iHeight),
			GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer.get());

		QImage frame(buffer.get(), static_cast<int>(m_iWidth), static_cast<int>(m_iHeight),
			QImage::Format_ARGB32);
		frame = frame.mirrored();

		if (m_bShrink && m_iNewWidth != m_iWidth && m_iNewHeight != m_iHeight)
			frame = frame.scaled(static_cast<int>(m_iNewWidth), static_cast<int>(m_iNewHeight),
				Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

		if (m_bGreyscale)
			frame = frame.convertToFormat(QImage::Format_Grayscale8);

		if (!m_bTransparent)
			frame = frame.convertToFormat(QImage::Format_RGB32);

		wxString filen = m_strFilename;
		filen << wxT("_") << i << wxT(".png");
		frame.save(QString::fromWCharArray(filen.wc_str()));

		if (g_canvas->root)
			g_canvas->root->tick(static_cast<float>(m_iTimeStep));
		if (g_canvas->sky)
			g_canvas->sky->tick(static_cast<float>(m_iTimeStep));
	}

#ifdef _WINDOWS
	if (video.supportPBO || video.supportVBO)
	{
		g_canvas->rt->EndRender();

		// Clear RenderTexture object.
		g_canvas->rt->Shutdown();
		wxDELETE(g_canvas->rt);
	}
#endif

	LOG_INFO << "PNG sequence successfully created.";

	g_canvas->model()->animManager->SetSpeed(m_fAnimSpeed);
	// Return the animation speed back to whatever it was previously set as
	g_canvas->model()->animManager->Play();

	Show(false);

	video.render = true;
	g_canvas->InitView(); //-V1020
}

void CAnimationExporter::OnButton(wxCommandEvent& event)
{
	if (event.GetId() == ID_GIFSTART)
	{
		CreateGif();
	}
	else if (event.GetId() == ID_GIFEXIT)
	{
		Show(false);
	}
}

void CAnimationExporter::OnCheck(wxCommandEvent& event)
{
	if (event.GetId() == ID_GIFTRANSPARENT)
	{
		m_bTransparent = event.IsChecked();
	}
	else if (event.GetId() == ID_GIFDIFFUSE)
	{
		m_bDiffuse = event.IsChecked();
	}
	else if (event.GetId() == ID_GIFSHRINK)
	{
		m_bShrink = event.IsChecked();
		txtSizeX->Enable(m_bShrink);
		txtSizeY->Enable(m_bShrink);
		if (m_bShrink)
		{
			const int screenSize[4]{};
			glGetIntegerv(GL_VIEWPORT, const_cast<GLint*>(screenSize)); // get the width/height of the canvas
			txtSizeX->Clear();
			*txtSizeX << screenSize[2];
			txtSizeY->Clear();
			*txtSizeY << screenSize[3];
		}
	}
	else if (event.GetId() == ID_GIFGREYSCALE)
	{
		m_bGreyscale = event.IsChecked();
	}
	else if (event.GetId() == ID_PNGSEQ)
	{
		m_bPng = event.IsChecked();
	}
}

#if defined(_WINDOWS)
void CAnimationExporter::CreateAvi(wxString fn)
{
	if (!g_canvas || !g_canvas->model() || !g_canvas->model()->animManager)
	{
		wxMessageBox(wxT("Unable to create AVI animation!"), wxT("Error"));
		LOG_ERROR << "Unable to created AVI animation.  A required object pointer was null!";
		return;
	}

	// Pause rendering to canvas
	video.render = false;

	// Save the old animation speed and set back to default
	m_fAnimSpeed = g_canvas->model()->animManager->GetSpeed();
	g_canvas->model()->animManager->SetSpeed(1.0f); // Set it to the normal speed.

	m_iTotalAnimFrames = g_canvas->model()->animManager->GetFrameCount();
	m_iTotalFrames = (m_iTotalAnimFrames / 25);

	if (video.supportPBO || video.supportVBO)
	{
		// if either are supported use our 'RenderTexture' object.
		g_canvas->rt = new RenderTexture();

		g_canvas->rt->Init(512, 512, video.supportFBO);

		m_iWidth = g_canvas->rt->nWidth;
		m_iHeight = g_canvas->rt->nHeight;

		//canvas->rt->BeginRender();
		//canvas->RenderToBuffer();
		//rt->BindTexture(); 
	}
	else
	{
		glReadBuffer(GL_BACK);
		int screenSize[4];
		glGetIntegerv(GL_VIEWPORT, screenSize); // get the width/height of the canvas
		m_iWidth = screenSize[2];
		m_iHeight = screenSize[3];
	}

	// will crash program - prevent this from happening
	if (m_iTotalFrames > m_iTotalAnimFrames)
	{
		wxMessageBox(
			wxT("Impossible to make a gif with more frames than the model animation.\nClosing gif exporter."),
			wxT("Error"));
		LOG_ERROR << "Unable to make a gif with more frames than the model animation.";
		return;
	}

	//const ssize_t timeStep = (m_iTotalAnimFrames / m_iTotalFrames);
	const ssize_t bufSize = m_iWidth * m_iHeight * 3; // (width*height*bytesPerPixel - only 3 for RGB, no alpha)  

	CAVIGenerator AviGen;

	BITMAPINFOHEADER bmHeader{};
	bmHeader.biWidth = static_cast<LONG>(m_iWidth);
	bmHeader.biHeight = static_cast<LONG>(m_iHeight);
	bmHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmHeader.biPlanes = 1;
	bmHeader.biBitCount = 24;
	bmHeader.biSizeImage = bufSize;
	bmHeader.biCompression = BI_RGB; //BI_RGB means BRG in reality
	bmHeader.biClrUsed = 0;
	bmHeader.biClrImportant = 0;
	bmHeader.biXPelsPerMeter = 0;
	bmHeader.biYPelsPerMeter = 0;

	// set our avi config
	AviGen.SetRate(25); // set 25fps
	AviGen.SetBitmapHeader(bmHeader);
	AviGen.SetFileName(fn.fn_str());

	AviGen.InitEngineForWrite(static_cast<HWND>(this->GetParent()->GetHandle()));

	// Stop our animation
	g_canvas->model()->animManager->Pause(true);
	g_canvas->model()->animManager->Stop();

	// Create one frame to make our optimal colour palette from.
	unsigned char* buffer = new unsigned char[bufSize];

	// Iterate through the frames saving the image to a buffer then writing it to the AVI
	for (unsigned int i = 0; i < m_iTotalFrames; i++)
	{
		g_canvas->RenderToBuffer();
		glReadPixels(0, 0, static_cast<GLsizei>(m_iWidth), static_cast<GLsizei>(m_iHeight), GL_BGR_EXT, GL_UNSIGNED_BYTE, buffer);
		AviGen.AddFrame(buffer);

		// not needed due to the code just below
		//g_canvas->model()->animManager->SetTimeDiff(timeStep);
		//g_canvas->model()->animManager->Tick(timeStep);

		// Animate particles
		if (g_canvas->root)
			g_canvas->root->tick(static_cast<float>(m_iTimeStep));
		if (g_canvas->sky)
			g_canvas->sky->tick(static_cast<float>(m_iTimeStep));
	}

	// Release our Avi writing object
	AviGen.ReleaseEngine();

	// Clear our pixel data buffer.
	wxDELETEA(buffer);

	// Clear the Render Texture object from memory if it exists.
	if (g_canvas->rt)
	{
		g_canvas->rt->EndRender();

		// Clear RenderTexture object.
		g_canvas->rt->Shutdown();
		wxDELETE(g_canvas->rt);
	}

	g_canvas->model()->animManager->SetSpeed(m_fAnimSpeed);
	// Return the animation speed back to whatever it was previously set as
	g_canvas->model()->animManager->Play();
	video.render = true;
	g_canvas->InitView();
}
#else
void CAnimationExporter::CreateAvi(wxString)
{
}
#endif
