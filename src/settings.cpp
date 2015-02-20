
#include "settings.h"
#include "enums.h"
#include "util.h"
#include "app.h"

#include "CASCFolder.h"
#include "core/GlobalSettings.h"

#include "globalvars.h"

IMPLEMENT_CLASS(Settings_Page1, wxWindow)
IMPLEMENT_CLASS(Settings_Page2, wxWindow)
IMPLEMENT_CLASS(SettingsControl, wxWindow)

BEGIN_EVENT_TABLE(Settings_Page1, wxWindow)
	EVT_CHECKBOX(ID_SETTINGS_RANDOMSKIN, Settings_Page1::OnCheck)
	EVT_CHECKBOX(ID_SETTINGS_SHOWPARTICLE, Settings_Page1::OnCheck)
	EVT_CHECKBOX(ID_SETTINGS_ZEROPARTICLE, Settings_Page1::OnCheck)
	EVT_BUTTON(ID_SETTINGS_PAGE1_APPLY, Settings_Page1::OnButton)
END_EVENT_TABLE()


BEGIN_EVENT_TABLE(Settings_Page2, wxWindow)
	EVT_BUTTON(ID_SETTINGS_PAGE2_APPLY, Settings_Page2::OnButton)
END_EVENT_TABLE()


BEGIN_EVENT_TABLE(SettingsControl, wxWindow)
	
END_EVENT_TABLE()


Settings_Page1::Settings_Page1(wxWindow* parent, wxWindowID id)
{
	if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("Settings_Page1")) == false) {
		wxLogMessage(wxT("GUI Error: Settings_Page1"));
		return;
	}

	chkbox[CHECK_SHOWPARTICLE] = new wxCheckBox(this, ID_SETTINGS_SHOWPARTICLE, _("Show Particle"), wxPoint(5,50), wxDefaultSize, 0);
	chkbox[CHECK_ZEROPARTICLE] = new wxCheckBox(this, ID_SETTINGS_ZEROPARTICLE, _("Zero Particle"), wxPoint(145,50), wxDefaultSize, 0);
	chkbox[CHECK_RANDOMSKIN] = new wxCheckBox(this, ID_SETTINGS_RANDOMSKIN, _("Random Skins"), wxPoint(285,50), wxDefaultSize, 0);

	new wxStaticText(this, wxID_ANY, _("Game path (including final \\Data statement - ie C:\\Games\\WoW\\Data)"),  wxPoint(5,90), wxDefaultSize, 0);
  gamePathCtrl =  new wxTextCtrl(this, wxID_ANY, gamePath, wxPoint(5,115), wxSize(300,-1), 0);
  new wxButton(this, ID_SETTINGS_PAGE1_APPLY, _("Apply"), wxPoint(315,110), wxDefaultSize, 0);
}


void Settings_Page1::OnCheck(wxCommandEvent &event)
{
	int id = event.GetId();

	if (id==ID_SETTINGS_RANDOMSKIN) {
		useRandomLooks = event.IsChecked();
	} else if (id==ID_SETTINGS_SHOWPARTICLE) {
	  GLOBALSETTINGS.bShowParticle = event.IsChecked();
	} else if (id==ID_SETTINGS_ZEROPARTICLE) {
	  GLOBALSETTINGS.bZeroParticle = event.IsChecked();
	}
}

void Settings_Page1::Update()
{
	chkbox[CHECK_RANDOMSKIN]->SetValue(useRandomLooks);
	chkbox[CHECK_SHOWPARTICLE]->SetValue(GLOBALSETTINGS.bShowParticle);
	chkbox[CHECK_ZEROPARTICLE]->SetValue(GLOBALSETTINGS.bZeroParticle);
	gamePathCtrl->SetValue(gamePath);
}

void Settings_Page1::OnButton(wxCommandEvent &event)
{
  if ( event.GetId() == ID_SETTINGS_PAGE1_APPLY)
  {
    if(gamePath !=  gamePathCtrl->GetValue())
    {
      gamePath = gamePathCtrl->GetValue();
      wxMessageBox(wxT("WoW Game Path changed.\nYou need to restart WoW Model Viewer to take it into account"), wxT("Settings Changed"), wxICON_INFORMATION);
      g_modelViewer->SaveSession();
    }
  }
}

Settings_Page2::Settings_Page2(wxWindow* parent, wxWindowID id)
{
	if (Create(parent, id, wxPoint(0,0), wxSize(400,400), 0, wxT("Settings_Page2")) == false) {
		wxLogMessage(wxT("GUI Error: Settings_Page2"));
		return;
	}

	wxFlexGridSizer *top = new wxFlexGridSizer(1);
	top->AddGrowableCol(0);
	top->SetFlexibleDirection(wxBOTH);

	top->Add(new wxStaticText(this, wxID_ANY, _("OpenGL Display Mode:"), wxDefaultPosition, wxDefaultSize, 0), 1, wxEXPAND|wxALL, 10);	
	top->Add(oglMode = new wxComboBox(this, wxID_ANY, _("Default"), wxDefaultPosition, wxSize(360, 25), 0, 0, wxCB_READONLY), 1, wxEXPAND, 10);
	
	top->Add(new wxStaticText(this, wxID_ANY, _("Field of View:"), wxDefaultPosition, wxDefaultSize, 0), 1, wxEXPAND|wxALL, 10);	
	top->Add(txtFov = new wxTextCtrl(this, wxID_ANY, wxT("45.000000"), wxDefaultPosition, wxSize(100, 20)), 1, 0, 10);

	wxFlexGridSizer *gs = new wxFlexGridSizer(3, 4, 4);
	
	#define ADD_CONTROLS(index, id, caption) \
	gs->Add(chkBox[index] = new wxCheckBox(this, id, caption, wxDefaultPosition, wxDefaultSize, 0), wxSizerFlags(0).Align(wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL));
	
	ADD_CONTROLS(CHECK_COMPRESSEDTEX, wxID_ANY, _("Compressed Textures"))
	ADD_CONTROLS(CHECK_MULTITEX, wxID_ANY, _("Multi-Textures"))
	ADD_CONTROLS(CHECK_VBO, wxID_ANY, _("Vertex Buffer"))
	ADD_CONTROLS(CHECK_FBO, wxID_ANY, _("Frame Buffer"))
	ADD_CONTROLS(CHECK_PBO, wxID_ANY, _("Pixel Buffer"))
	ADD_CONTROLS(CHECK_DRAWRANGEELEMENTS, wxID_ANY, _("Draw Range Elements"))
	ADD_CONTROLS(CHECK_ENVMAPPING, wxID_ANY, _("Environmental Mapping"))
	ADD_CONTROLS(CHECK_NPOT, wxID_ANY, _("Non-Power of two"))
	ADD_CONTROLS(CHECK_PIXELSHADERS, wxID_ANY, _("Pixel Shaders"))
	ADD_CONTROLS(CHECK_VERTEXSHADERS, wxID_ANY, _("Vertex Shaders"))
	ADD_CONTROLS(CHECK_GLSLSHADERS, wxID_ANY, _("GLSL Shaders"))
	#undef ADD_CONTROLS

	top->Add(gs,wxSizerFlags().Proportion(1).Expand().Border(wxALL, 10));

	top->Add(new wxButton(this, ID_SETTINGS_PAGE2_APPLY, _("Apply Settings"), wxDefaultPosition, wxDefaultSize, 0), wxSizerFlags()/*.Expand()*/.Border(wxALL, 10).Align(wxALIGN_LEFT|wxALIGN_BOTTOM));
	
	top->SetMinSize(350, 350);
	//top->SetMaxSize(400, 400);
	top->SetSizeHints(this);
	SetSizer(top);
	SetAutoLayout(true);
	Layout();
}

void Settings_Page2::Update()
{
	oglMode->Clear();

	for (size_t i=0; i<video.capsList.size(); i++) {
		wxString mode = wxString::Format(wxT("Colour:%i Depth:%i Alpha:%i "), video.capsList[i].colour, video.capsList[i].zBuffer, video.capsList[i].alpha);
		
		if (video.capsList[i].sampleBuffer)
			mode.Append(wxString::Format(wxT("FSAA:%i "), video.capsList[i].aaSamples));
		
		if (video.capsList[i].doubleBuffer)
			mode.Append(wxT("DoubleBuffer "));

#ifdef _WINDOWS
		if (video.capsList[i].hwAcc == WGL_FULL_ACCELERATION_ARB)
			mode.Append(wxT("Hardware mode"));
		else if (video.capsList[i].hwAcc == WGL_GENERIC_ACCELERATION_ARB)
			mode.Append(wxT("Emulation mode"));
		else //WGL_NO_ACCELERATION_ARB
			mode.Append(wxT("Software mode"));
#endif

		oglMode->Append(mode);
	}

	oglMode->SetSelection(video.capIndex);

	txtFov->SetValue(wxString::Format(wxT("%f"), video.fov));

	// Toggle all the video options
	if (video.supportCompression)
		chkBox[CHECK_COMPRESSEDTEX]->SetValue(video.useCompression);
	else
		chkBox[CHECK_COMPRESSEDTEX]->Disable();

	if (video.supportMultiTex) {
		chkBox[CHECK_MULTITEX]->SetValue(true);
		chkBox[CHECK_MULTITEX]->Disable();
	} else
		chkBox[CHECK_MULTITEX]->Disable();

	if (video.supportVBO)
		chkBox[CHECK_VBO]->SetValue(video.useVBO);
	else
		chkBox[CHECK_VBO]->Disable();

	if (video.supportFBO)
		chkBox[CHECK_FBO]->SetValue(video.useFBO);
	else
		chkBox[CHECK_FBO]->Disable();

	if (video.supportPBO)
		chkBox[CHECK_PBO]->SetValue(video.usePBO);
	else
		chkBox[CHECK_PBO]->Disable();

	if (video.supportDrawRangeElements) {
		chkBox[CHECK_DRAWRANGEELEMENTS]->SetValue(true);
		chkBox[CHECK_DRAWRANGEELEMENTS]->Disable();
	} else
		chkBox[CHECK_DRAWRANGEELEMENTS]->Disable();

	chkBox[CHECK_ENVMAPPING]->SetValue(video.useEnvMapping);

	if (video.supportNPOT) {
		chkBox[CHECK_NPOT]->SetValue(true);
		chkBox[CHECK_NPOT]->Disable();
	} else
		chkBox[CHECK_NPOT]->Disable();

	if (video.supportFragProg)
		chkBox[CHECK_PIXELSHADERS]->SetValue(true);
	else
		chkBox[CHECK_PIXELSHADERS]->Disable();

	if (video.supportVertexProg)
		chkBox[CHECK_VERTEXSHADERS]->SetValue(true);
	else
		chkBox[CHECK_VERTEXSHADERS]->Disable();

	if (video.supportGLSL)
		chkBox[CHECK_GLSLSHADERS]->SetValue(true);
	else
		chkBox[CHECK_GLSLSHADERS]->Disable();
}

void Settings_Page2::OnButton(wxCommandEvent &event)
{
	int id = event.GetId();
	
	if (id == ID_SETTINGS_PAGE2_APPLY) {
		if ((oglMode->GetSelection() != video.capIndex) && video.GetCompatibleWinMode(video.capsList[oglMode->GetSelection()])) {
			wxLogMessage(wxT("Info: Graphics display mode changed.  Requires restart to take effect."));
			wxMessageBox(wxT("Graphics display settings changed.\nWoW Model Viewer requires restarting to take effect."), wxT("Settings Changed"), wxICON_INFORMATION);
		}
		
		double fov;
		txtFov->GetValue().ToDouble(&fov);
		if ((fov > 0) && (fov < 270.0))
			video.fov = (float) fov;

		g_modelViewer->SaveSession();
		g_modelViewer->interfaceManager.GetPane(this->GetParent()).Show(false);
		g_modelViewer->interfaceManager.Update();
	}
}

SettingsControl::SettingsControl(wxWindow* parent, wxWindowID id)
{
	wxLogMessage(wxT("Creating Settings Control..."));
	
	if (Create(parent, id, wxDefaultPosition, wxSize(405,440), wxDEFAULT_FRAME_STYLE, wxT("SettingsControlFrame")) == false) {
		wxLogMessage(wxT("GUI Error: Failed to create the window for our SettingsControl!"));
		return;
	}

	//
	notebook = new wxNotebook(this, ID_SETTINGS_TABS, wxPoint(0,0), wxSize(400,420), wxNB_TOP|wxNB_FIXEDWIDTH|wxNB_NOPAGETHEME);
	
	page1 = new Settings_Page1(notebook, ID_SETTINGS_PAGE1);
	page2 = new Settings_Page2(notebook, ID_SETTINGS_PAGE2);

	notebook->AddPage(page1, _("Options"), false, -1);
	notebook->AddPage(page2, _("Display"), false);
}


SettingsControl::~SettingsControl()
{
	//page1->Destroy();
	//page2->Destroy();
	//notebook->Destroy();
}


void SettingsControl::Open()
{
	Show(true);

	page1->Update();
	page2->Update();
}

void SettingsControl::Close()
{
	
}

// --
