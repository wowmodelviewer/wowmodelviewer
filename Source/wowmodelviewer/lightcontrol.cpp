#include "lightcontrol.h"
#include <glm/gtc/type_ptr.hpp>
#include "enums.h"
#include "logger/Logger.h"
#include <wx/colordlg.h>
#include "GL/glew.h"

extern const size_t MAX_LIGHTS;

// default colour values
const static glm::vec4 def_ambience = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
const static glm::vec4 def_diffuse = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
const static glm::vec4 def_specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

// Inhereit class event table from wxWindow
IMPLEMENT_CLASS(LightControl, wxWindow)

// Event table for GUI controls
BEGIN_EVENT_TABLE(LightControl, wxWindow)
	//EVT_ONSIZE()
	EVT_BUTTON(ID_LIGHTCOLOUR, LightControl::OnButton)
	EVT_BUTTON(ID_LIGHTAMBIENCE, LightControl::OnButton)
	EVT_BUTTON(ID_LIGHTDIFFUSE, LightControl::OnButton)
	EVT_BUTTON(ID_LIGHTSPECULAR, LightControl::OnButton)
	EVT_BUTTON(ID_LIGHTRESET, LightControl::OnButton)
	EVT_COMBOBOX(ID_LIGHTSEL, LightControl::OnCombo)
	EVT_CHECKBOX(ID_LIGHTENABLED, LightControl::OnCheck)
	EVT_CHECKBOX(ID_LIGHTRELATIVE, LightControl::OnCheck)
	EVT_RADIOBUTTON(ID_LIGHTPOSITIONAL, LightControl::OnRadio)
	EVT_RADIOBUTTON(ID_LIGHTSPOT, LightControl::OnRadio)
	EVT_RADIOBUTTON(ID_LIGHTDIRECTIONAL, LightControl::OnRadio)
	EVT_TEXT_ENTER(ID_LIGHTPOSX, LightControl::OnText)
	EVT_TEXT_ENTER(ID_LIGHTPOSY, LightControl::OnText)
	EVT_TEXT_ENTER(ID_LIGHTPOSZ, LightControl::OnText)
	EVT_TEXT_ENTER(ID_LIGHTTARX, LightControl::OnText)
	EVT_TEXT_ENTER(ID_LIGHTTARY, LightControl::OnText)
	EVT_TEXT_ENTER(ID_LIGHTTARZ, LightControl::OnText)
	EVT_COMMAND_SCROLL(ID_LIGHTCINTENSITY, LightControl::OnScroll)
	EVT_COMMAND_SCROLL(ID_LIGHTLINTENSITY, LightControl::OnScroll)
	EVT_COMMAND_SCROLL(ID_LIGHTQINTENSITY, LightControl::OnScroll)
	EVT_COMMAND_SCROLL(ID_LIGHTALPHA, LightControl::OnScroll)
END_EVENT_TABLE()

LightControl::LightControl(wxWindow* parent, wxWindowID id)
{
	LOG_INFO << "Creating Light Control...";

	activeLight = 0;
	lights = nullptr;
	lights = new Light[MAX_LIGHTS];

	if (Create(parent, id, wxDefaultPosition, wxSize(160, 430), 0, wxT("LightControlFrame")) == false)
	{
		LOG_ERROR << "Failed to create a window frame for the LightControl!";
		return;
	}

	wxArrayString choices;
	for (size_t i = 1; i <= MAX_LIGHTS; i++)
	{
		wxString s = wxT("Light ");
		s += wxString::Format(wxT("%i"), i);

		choices.Add(s);
	}

	lightSel = new wxComboBox(this, ID_LIGHTSEL, wxT("Light 1"), wxPoint(20, 10), wxSize(100, 20), choices,
	                          wxCB_READONLY);

	enabled = new wxCheckBox(this, ID_LIGHTENABLED, wxT("Enabled"), wxPoint(10, 40), wxDefaultSize);
	relative = new wxCheckBox(this, ID_LIGHTRELATIVE, wxT("Relative"), wxPoint(70, 40), wxDefaultSize);

	positional = new wxRadioButton(this, ID_LIGHTPOSITIONAL, wxT("Pos"), wxPoint(4, 60), wxDefaultSize, 0);
	spot = new wxRadioButton(this, ID_LIGHTSPOT, wxT("Spot"), wxPoint(45, 60), wxDefaultSize, 0);
	directional = new wxRadioButton(this, ID_LIGHTDIRECTIONAL, wxT("Dir"), wxPoint(90, 60), wxDefaultSize, 0);

	positional->SetValue(false);
	spot->SetValue(false);
	directional->SetValue(true);

	// wxRadioButton(wxWindow* parent, wxWindowID id, const wxString& label, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0, const wxValidator& validator = wxDefaultValidator, const wxString& name = "radioButton")

	// Not needed.  Keep code just in case.
	//lblCol = new wxStaticText(this, wxID_ANY, wxT("Colour"), wxPoint(5,60), wxDefaultSize);
	//colour = new wxButton(this, ID_LIGHTCOLOUR, "", wxPoint(60, 60), wxSize(60,20));

	lblAmb = new wxStaticText(this, wxID_ANY, wxT("Ambience"), wxPoint(5, 82), wxDefaultSize);
	ambience = new wxButton(this, ID_LIGHTAMBIENCE, wxEmptyString, wxPoint(60, 82), wxSize(60, 20));

	lblDiff = new wxStaticText(this, wxID_ANY, wxT("Diffuse"), wxPoint(5, 104), wxDefaultSize);
	diffuse = new wxButton(this, ID_LIGHTDIFFUSE, wxEmptyString, wxPoint(60, 104), wxSize(60, 20));

	lblSpec = new wxStaticText(this, wxID_ANY, wxT("Specular"), wxPoint(5, 126), wxDefaultSize);
	specular = new wxButton(this, ID_LIGHTSPECULAR, wxEmptyString, wxPoint(60, 126), wxSize(60, 20));

	//position
	lblPos = new wxStaticText(this, wxID_ANY, wxT("Position XYZ"), wxPoint(5, 155), wxDefaultSize);
	txtPosX = new wxTextCtrl(this, ID_LIGHTPOSX, wxT("0.0"), wxPoint(5, 175), wxSize(60, 20), wxTE_PROCESS_ENTER);
	txtPosY = new wxTextCtrl(this, ID_LIGHTPOSY, wxT("0.0"), wxPoint(5, 195), wxSize(60, 20), wxTE_PROCESS_ENTER);
	txtPosZ = new wxTextCtrl(this, ID_LIGHTPOSZ, wxT("0.0"), wxPoint(5, 215), wxSize(60, 20), wxTE_PROCESS_ENTER);

	//position
	lblTar = new wxStaticText(this, wxID_ANY, wxT("Target XYZ"), wxPoint(80, 155), wxDefaultSize);
	txtTarX = new wxTextCtrl(this, ID_LIGHTTARX, wxT("0.0"), wxPoint(80, 175), wxSize(60, 20), wxTE_PROCESS_ENTER);
	txtTarY = new wxTextCtrl(this, ID_LIGHTTARY, wxT("0.0"), wxPoint(80, 195), wxSize(60, 20), wxTE_PROCESS_ENTER);
	txtTarZ = new wxTextCtrl(this, ID_LIGHTTARZ, wxT("0.0"), wxPoint(80, 215), wxSize(60, 20), wxTE_PROCESS_ENTER);

	lblIntensity = new wxStaticText(this, wxID_ANY, wxT("Light Attenuation:\nConstant / Linear / Quadradic"),
	                                wxPoint(5, 240), wxDefaultSize);
	cintensity = new wxSlider(this, ID_LIGHTCINTENSITY, 0, 0, 100, wxPoint(45, 275), wxSize(100, 30),
	                          wxSL_HORIZONTAL | wxSL_LABELS);
	lintensity = new wxSlider(this, ID_LIGHTLINTENSITY, 0, 0, 100, wxPoint(45, 310), wxSize(100, 30),
	                          wxSL_HORIZONTAL | wxSL_LABELS);
	qintensity = new wxSlider(this, ID_LIGHTQINTENSITY, 0, 0, 100, wxPoint(45, 340), wxSize(100, 30),
	                          wxSL_HORIZONTAL | wxSL_LABELS);

	lblAlpha = new wxStaticText(this, wxID_ANY, wxT("Arc:"), wxPoint(2, 375), wxDefaultSize);
	alpha = new wxSlider(this, ID_LIGHTALPHA, 0, 0, 90, wxPoint(45, 375), wxSize(100, 30),
	                     wxSL_HORIZONTAL | wxSL_LABELS);
	alpha->Enable(false);

	reset = new wxButton(this, ID_LIGHTRESET, wxT("Reset"), wxPoint(50, 405), wxSize(60, 20));

	//Init();
	//Update();
}

LightControl::~LightControl()
{
	/*
	//lblCol->Destroy();
	lblDiff->Destroy();
	lblAmb->Destroy();
	lblSpec->Destroy();
	txtPosX->Destroy();
	txtPosY->Destroy();
	txtPosZ->Destroy();
	
	txtTarX->Destroy();
	txtTarY->Destroy();
	txtTarZ->Destroy();
	
	enabled->Destroy();
	relative->Destroy();
	
	diffuse->Destroy();
	ambience->Destroy();
	specular->Destroy();
	//update->Destroy();
	reset->Destroy();
	
	lightSel->Destroy();
	
	cintensity->Destroy();
	lintensity->Destroy();
	qintensity->Destroy();
	alpha->Destroy();
	positional->Destroy();
	spot->Destroy();
	directional->Destroy();
	*/
	wxDELETEA(lights);
}

void LightControl::Init()
{
	//glGetLightiv(GL_LIGHT0, GL_MAX_LIGHTS, maxlights);
	//LOG_INFO << "Max Lights Supported:" << maxlights;
	LOG_INFO << "Max Lights used:" << MAX_LIGHTS;

	if (!lights)
		return;

	// Set default values.
	for (size_t i = 0; i < MAX_LIGHTS; i++)
	{
		lights[i].ambience = def_ambience;
		lights[i].diffuse = def_diffuse;
		lights[i].specular = def_specular;
		//lights[i].colour = glm::vec3(1.0f, 1.0f, 1.0f);
		lights[i].pos = glm::vec4(0.0f, 0.2f, 1.0f, 1.0f);
		lights[i].target = glm::vec4(0.0f, -1.0f, 0.0f, 1.0f);
		lights[i].enabled = false;
		lights[i].relative = false;
		lights[i].type = LIGHT_DIRECTIONAL;
		lights[i].constant_int = 1.0f;
		lights[i].linear_int = 0.0f;
		lights[i].quadradic_int = 0.0f;
		lights[i].arc = 90.0f;
	}

	// Turn on the first light by default
	lights[0].enabled = true;
	glEnable(GL_LIGHT0);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, glm::value_ptr(lights[0].diffuse));
	glLightfv(GL_LIGHT0, GL_AMBIENT, glm::value_ptr(lights[0].ambience));
	glLightfv(GL_LIGHT0, GL_SPECULAR, glm::value_ptr(lights[0].specular));
	glLightfv(GL_LIGHT0, GL_POSITION, glm::value_ptr(lights[0].pos));

	Update();
}

void LightControl::SetColour()
{
	/*
	lights[activeLight].colour = DoSetColour(lights[activeLight].colour);
  
	wxColour col((unsigned char)(lights[activeLight].colour.x*255.0f), (unsigned char)(lights[activeLight].colour.y*255.0f), (unsigned char)(lights[activeLight].colour.z*255.0f));
	colour->SetBackgroundColour(col);
	*/
}

void LightControl::SetAmbience()
{
	lights[activeLight].ambience = DoSetColour(lights[activeLight].ambience);

	glLightfv(GL_LIGHT0 + activeLight, GL_AMBIENT, glm::value_ptr(lights[activeLight].ambience));

	const wxColour col(static_cast<unsigned char>(lights[activeLight].ambience.x * 255.0f),
	                   static_cast<unsigned char>(lights[activeLight].ambience.y * 255.0f),
	                   static_cast<unsigned char>(lights[activeLight].ambience.z * 255.0f));
	ambience->SetBackgroundColour(col);
}

void LightControl::SetDiffuse()
{
	lights[activeLight].diffuse = DoSetColour(lights[activeLight].diffuse);

	// Update the opengl scene lighting
	glLightfv(GL_LIGHT0 + activeLight, GL_DIFFUSE, glm::value_ptr(lights[activeLight].diffuse));

	const wxColour col(static_cast<unsigned char>(lights[activeLight].diffuse.x * 255.0f),
	                   static_cast<unsigned char>(lights[activeLight].diffuse.y * 255.0f),
	                   static_cast<unsigned char>(lights[activeLight].diffuse.z * 255.0f));
	diffuse->SetBackgroundColour(col);
}

void LightControl::SetPos(glm::vec4 p)
{
	lights[activeLight].pos = p;
}

void LightControl::SetTarget(glm::vec4 t)
{
	lights[activeLight].target = t;
}

void LightControl::SetSpecular()
{
	lights[activeLight].specular = DoSetColour(lights[activeLight].specular);

	glLightfv(GL_LIGHT0 + activeLight, GL_SPECULAR, glm::value_ptr(lights[activeLight].specular));

	const wxColour col(static_cast<unsigned char>(lights[activeLight].specular.x * 255.0f),
	                   static_cast<unsigned char>(lights[activeLight].specular.y * 255.0f),
	                   static_cast<unsigned char>(lights[activeLight].specular.z * 255.0f));
	specular->SetBackgroundColour(col);
}

void LightControl::OnButton(wxCommandEvent& event)
{
	const int id = event.GetId();

	if (id == ID_LIGHTCOLOUR)
		SetColour();
	else if (id == ID_LIGHTAMBIENCE)
		SetAmbience();
	else if (id == ID_LIGHTDIFFUSE)
		SetDiffuse();
	else if (id == ID_LIGHTSPECULAR)
		SetSpecular();
	else if (id == ID_LIGHTRESET)
	{
		lights[activeLight].ambience = def_ambience;
		lights[activeLight].diffuse = def_diffuse;
		lights[activeLight].specular = def_specular;
		//lights[activeLight].colour = glm::vec3(1.0f, 1.0f, 1.0f);
		lights[activeLight].pos = glm::vec4(0.0f, 0.2f, 1.0f, 1.0f);
		lights[activeLight].target = glm::vec4(0.0f, -1.0f, 0.0f, 1.0f);
		lights[activeLight].enabled = (activeLight == 0) ? true : false;
		lights[activeLight].relative = false;
		lights[activeLight].type = LIGHT_DIRECTIONAL;
		lights[activeLight].arc = 90.0f;
		lights[activeLight].constant_int = 1.0f;
		lights[activeLight].linear_int = 0.0f;
		lights[activeLight].quadradic_int = 0.0f;

		glLightfv(GL_LIGHT0 + activeLight, GL_DIFFUSE, glm::value_ptr(lights[activeLight].diffuse));
		glLightfv(GL_LIGHT0 + activeLight, GL_AMBIENT, glm::value_ptr(lights[activeLight].ambience));
		glLightfv(GL_LIGHT0 + activeLight, GL_SPECULAR, glm::value_ptr(lights[activeLight].specular));

		Update();
	}
}

void LightControl::OnCombo(wxCommandEvent& event)
{
	if (event.GetId() == ID_LIGHTSEL)
	{
		activeLight = lightSel->GetSelection();
		Update();
	}
}

void LightControl::OnText(wxCommandEvent& event)
{
	const int id = event.GetId();
	glm::vec4 p = lights[activeLight].pos;
	glm::vec4 t = lights[activeLight].target;

	if (id == ID_LIGHTPOSX)
		from_string<float>(p.x, std::string(txtPosX->GetValue().mb_str()), dec);
	else if (id == ID_LIGHTPOSY)
		from_string<float>(p.y, std::string(txtPosY->GetValue().mb_str()), dec);
	else if (id == ID_LIGHTPOSZ)
		from_string<float>(p.z, std::string(txtPosZ->GetValue().mb_str()), dec);
	else if (id == ID_LIGHTTARX)
		from_string<float>(t.x, std::string(txtTarX->GetValue().mb_str()), dec);
	else if (id == ID_LIGHTTARY)
		from_string<float>(t.y, std::string(txtTarY->GetValue().mb_str()), dec);
	else if (id == ID_LIGHTTARZ)
		from_string<float>(t.z, std::string(txtTarZ->GetValue().mb_str()), dec);

	SetPos(p);
	SetTarget(t);
}

void LightControl::OnCheck(wxCommandEvent& event)
{
	if (event.GetId() == ID_LIGHTENABLED)
	{
		lights[activeLight].enabled = event.IsChecked();

		const GLuint light = GL_LIGHT0 + activeLight;
		if (lights[activeLight].enabled)
			glEnable(light);
		else
			glDisable(light);
	}
	else if (event.GetId() == ID_LIGHTRELATIVE)
	{
		lights[activeLight].relative = event.IsChecked();
	}
}

void LightControl::OnRadio(wxCommandEvent& event)
{
	cintensity->Enable(false);
	lintensity->Enable(false);
	qintensity->Enable(false);
	alpha->Enable(false);

	const GLuint lightID = GL_LIGHT0 + activeLight;

	if (event.GetId() == ID_LIGHTPOSITIONAL)
	{
		lights[activeLight].type = LIGHT_POSITIONAL;
		lights[activeLight].pos.w = 1.0f;

		cintensity->Enable(true);
		lintensity->Enable(true);
		qintensity->Enable(true);

		glLightf(lightID, GL_CONSTANT_ATTENUATION, lights[activeLight].constant_int);
		glLightf(lightID, GL_LINEAR_ATTENUATION, lights[activeLight].linear_int);
		glLightf(lightID, GL_QUADRATIC_ATTENUATION, lights[activeLight].quadradic_int);

		glLightf(lightID, GL_SPOT_CUTOFF, 180.0f);
	}
	else if (event.GetId() == ID_LIGHTSPOT)
	{
		lights[activeLight].type = LIGHT_SPOT;
		lights[activeLight].pos.w = 1.0f;

		alpha->Enable(true);

		//glLightf(lightID, GL_SPOT_EXPONENT, 8.0f);          // This seems to have no effect?
		glLightf(lightID, GL_SPOT_CUTOFF, lights[activeLight].arc); // Lighting arc
		glLightfv(lightID, GL_SPOT_DIRECTION, glm::value_ptr(lights[activeLight].target)); // Lighting target
		//glLightf(lightID, GL_SPOT_ATTENUATION, lights[i].constant_int);
	}
	else if (event.GetId() == ID_LIGHTDIRECTIONAL)
	{
		lights[activeLight].type = LIGHT_DIRECTIONAL;
		lights[activeLight].pos.w = 0.0f;
	}
}

void LightControl::OnScroll(wxScrollEvent& event)
{
	if (event.GetId() == ID_LIGHTCINTENSITY)
	{
		lights[activeLight].constant_int = (event.GetInt() / 100.0f);
	}
	else if (event.GetId() == ID_LIGHTLINTENSITY)
	{
		lights[activeLight].linear_int = (event.GetInt() / 100.0f);
	}
	else if (event.GetId() == ID_LIGHTQINTENSITY)
	{
		lights[activeLight].quadradic_int = (event.GetInt() / 100.0f);
	}
	else if (event.GetId() == ID_LIGHTALPHA)
	{
		lights[activeLight].arc = static_cast<float>(event.GetInt());
	}

	//
	// Update our light settings here
	// instead of calling it every frame
	const GLuint lightID = GL_LIGHT0 + activeLight;

	if (lights[activeLight].type == LIGHT_POSITIONAL)
	{
		// Attenuation is ignored for directional lighting, so only set if its positional
		glLightf(lightID, GL_CONSTANT_ATTENUATION, lights[activeLight].constant_int);
		glLightf(lightID, GL_LINEAR_ATTENUATION, lights[activeLight].linear_int);
		glLightf(lightID, GL_QUADRATIC_ATTENUATION, lights[activeLight].quadradic_int);

		glLightf(lightID, GL_SPOT_CUTOFF, 180.0f);
		//glLightf(lightID, GL_SPOT_EXPONENT, 0.0f);  
	}
	else if (lights[activeLight].type == LIGHT_DIRECTIONAL)
	{
		// Directional
	}
	else
	{
		// its a spot light
		//glLightf(lightID, GL_SPOT_EXPONENT, 8.0f);          // This seems to have no effect?
		glLightf(lightID, GL_SPOT_CUTOFF, lights[activeLight].arc); // Lighting arc
		glLightfv(lightID, GL_SPOT_DIRECTION, glm::value_ptr(lights[activeLight].target)); // Lighting target
		//glLightf(lightID, GL_SPOT_ATTENUATION, lights[i].constant_int);
	}
}

Light LightControl::GetCurrentLight()
{
	return lights[activeLight];
}

glm::vec4 LightControl::GetCurrentPos()
{
	return lights[activeLight].pos;
}

glm::vec4 LightControl::GetCurrentAmbience()
{
	return lights[activeLight].ambience;
}

glm::vec3 LightControl::DoSetColour(const glm::vec3& defColor)
{
	wxColourData data;
	const wxColour dcol(static_cast<unsigned char>(defColor.x * 255.0f), static_cast<unsigned char>(defColor.y * 255.0f),
	                    static_cast<unsigned char>(defColor.z * 255.0f));
	data.SetChooseFull(true);
	data.SetColour(dcol);

	wxColourDialog dialog(this, &data);
	if (dialog.ShowModal() == wxID_OK)
	{
		wxColourData retData = dialog.GetColourData();
		const wxColour col = retData.GetColour();
		return glm::vec3(col.Red() / 255.0f, col.Green() / 255.0f, col.Blue() / 255.0f);
	}
	return defColor;
}

glm::vec4 LightControl::DoSetColour(const glm::vec4& defColor)
{
	wxColourData data;
	const wxColour dcol(static_cast<unsigned char>(defColor.x * 255.0f), static_cast<unsigned char>(defColor.y * 255.0f),
	                    static_cast<unsigned char>(defColor.z * 255.0f));
	data.SetChooseFull(true);
	data.SetColour(dcol);

	wxColourDialog dialog(this, &data);
	if (dialog.ShowModal() == wxID_OK)
	{
		wxColourData retData = dialog.GetColourData();
		const wxColour col = retData.GetColour();
		return glm::vec4(col.Red() / 255.0f, col.Green() / 255.0f, col.Blue() / 255.0f, 1.0f);
	}
	return defColor;
}

void LightControl::Update()
{
	// Update the colours on each of the buttons.
	// Colour
	//wxColour col((unsigned char)(lights[activeLight].colour.x*255.0f), (unsigned char)(lights[activeLight].colour.y*255.0f), (unsigned char)(lights[activeLight].colour.z*255.0f));
	//colour->SetBackgroundColour(col);

	// Ambience
	wxColour col = wxColour(static_cast<unsigned char>(lights[activeLight].ambience.x * 255.0f),
	                        static_cast<unsigned char>(lights[activeLight].ambience.y * 255.0f),
	                        static_cast<unsigned char>(lights[activeLight].ambience.z * 255.0f));
	ambience->SetBackgroundColour(col);

	// Diffuse
	col = wxColour(static_cast<unsigned char>(lights[activeLight].diffuse.x * 255.0f),
	               static_cast<unsigned char>(lights[activeLight].diffuse.y * 255.0f),
	               static_cast<unsigned char>(lights[activeLight].diffuse.z * 255.0f));
	diffuse->SetBackgroundColour(col);

	// Specular
	col = wxColour(static_cast<unsigned char>(lights[activeLight].specular.x * 255.0f),
	               static_cast<unsigned char>(lights[activeLight].specular.y * 255.0f),
	               static_cast<unsigned char>(lights[activeLight].specular.z * 255.0f));
	specular->SetBackgroundColour(col);
	// -- -- -- --

	// enabled?
	enabled->SetValue(lights[activeLight].enabled);
	relative->SetValue(lights[activeLight].relative);

	positional->SetValue(false);
	spot->SetValue(false);
	directional->SetValue(false);

	// position
	wxString pos;

	pos << lights[activeLight].pos.x;
	txtPosX->SetValue(pos);
	pos = wxEmptyString;

	pos << lights[activeLight].pos.y;
	txtPosY->SetValue(pos);
	pos = wxEmptyString;

	pos << lights[activeLight].pos.z;
	txtPosZ->SetValue(pos);
	pos = wxEmptyString;
	// -- -- --

	// target
	pos << lights[activeLight].target.x;
	txtTarX->SetValue(pos);
	pos = wxEmptyString;

	pos << lights[activeLight].target.y;
	txtTarY->SetValue(pos);
	pos = wxEmptyString;

	pos << lights[activeLight].target.z;
	txtTarZ->SetValue(pos);
	// -- -- --

	cintensity->SetValue(static_cast<int>(lights[activeLight].constant_int * 100));
	lintensity->SetValue(static_cast<int>(lights[activeLight].linear_int * 100));
	qintensity->SetValue(static_cast<int>(lights[activeLight].quadradic_int * 100));
	alpha->SetValue(static_cast<int>(lights[activeLight].arc));

	cintensity->Enable(false);
	lintensity->Enable(false);
	qintensity->Enable(false);
	alpha->Enable(false);

	// Update the lighting
	const GLuint lightID = GL_LIGHT0 + activeLight;

	if (lights[activeLight].type == LIGHT_POSITIONAL)
	{
		positional->SetValue(true);
		cintensity->Enable(true);
		lintensity->Enable(true);
		qintensity->Enable(true);

		glLightf(lightID, GL_CONSTANT_ATTENUATION, lights[activeLight].constant_int);
		glLightf(lightID, GL_LINEAR_ATTENUATION, lights[activeLight].linear_int);
		glLightf(lightID, GL_QUADRATIC_ATTENUATION, lights[activeLight].quadradic_int);

		glLightf(lightID, GL_SPOT_CUTOFF, 180.0f);
	}
	else if (lights[activeLight].type == LIGHT_SPOT)
	{
		spot->SetValue(true);
		alpha->Enable(true);

		//glLightf(lightID, GL_SPOT_EXPONENT, 8.0f);            // This seems to have no effect?
		glLightf(lightID, GL_SPOT_CUTOFF, lights[activeLight].arc); // Lighting arc
		glLightfv(lightID, GL_SPOT_DIRECTION, glm::value_ptr(lights[activeLight].target)); // Lighting target
		//glLightf(lightID, GL_SPOT_ATTENUATION, lights[i].constant_int);
	}
	else if (lights[activeLight].type == LIGHT_DIRECTIONAL)
	{
		glLightf(lightID, GL_SPOT_CUTOFF, 180.0f);

		directional->SetValue(true);
	}

	//glEnable(GL_LIGHT0);
	glLightfv(GL_LIGHT0 + activeLight, GL_DIFFUSE, glm::value_ptr(lights[activeLight].diffuse));
	glLightfv(GL_LIGHT0 + activeLight, GL_AMBIENT, glm::value_ptr(lights[activeLight].ambience));
	glLightfv(GL_LIGHT0 + activeLight, GL_SPECULAR, glm::value_ptr(lights[activeLight].specular));
	glLightfv(GL_LIGHT0 + activeLight, GL_POSITION, glm::value_ptr(lights[activeLight].pos));
}

void LightControl::UpdateGL()
{
	const float tar[3] = {0.0f, -1.0f, 0.0f};
	// Update the lighting
	for (size_t i = 0; i < MAX_LIGHTS; i++)
	{
		const GLuint lightID = GL_LIGHT0 + (GLuint)i;

		if (lights[i].enabled)
			glEnable(lightID);
		else
			glDisable(lightID);

		glLightfv(GL_LIGHT0 + activeLight, GL_DIFFUSE, glm::value_ptr(lights[i].diffuse));
		glLightfv(GL_LIGHT0 + activeLight, GL_AMBIENT, glm::value_ptr(lights[i].ambience));
		glLightfv(GL_LIGHT0 + activeLight, GL_SPECULAR, glm::value_ptr(lights[i].specular));
		glLightfv(GL_LIGHT0 + activeLight, GL_POSITION, glm::value_ptr(lights[i].pos));

		glLightf(lightID, GL_CONSTANT_ATTENUATION, 1.0f);
		glLightf(lightID, GL_LINEAR_ATTENUATION, 0.0f);
		glLightf(lightID, GL_QUADRATIC_ATTENUATION, 0.0f);
		glLightf(lightID, GL_SPOT_CUTOFF, 180.0f);

		glLightfv(lightID, GL_SPOT_DIRECTION, tar);

		if (lights[i].type == LIGHT_POSITIONAL)
		{
			glLightf(lightID, GL_CONSTANT_ATTENUATION, lights[activeLight].constant_int);
			glLightf(lightID, GL_LINEAR_ATTENUATION, lights[activeLight].linear_int);
			glLightf(lightID, GL_QUADRATIC_ATTENUATION, lights[activeLight].quadradic_int);
		}
		else if (lights[activeLight].type == LIGHT_SPOT)
		{
			//glLightf(lightID, GL_SPOT_EXPONENT, 8.0f);            // This seems to have no effect?
			glLightf(lightID, GL_SPOT_CUTOFF, lights[i].arc); // Lighting arc
			glLightfv(lightID, GL_SPOT_DIRECTION, glm::value_ptr(lights[i].target)); // Lighting target
		}
	}
}
