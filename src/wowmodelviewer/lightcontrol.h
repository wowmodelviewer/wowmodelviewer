#ifndef LIGHTCONTROL_H
#define LIGHTCONTROL_H

// wxWidgets
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif


// OpenGL headers
#include "OpenGLHeaders.h"

#include "enums.h"

// Vector types
#include "vec3d.h"
#include "quaternion.h"
#include "util.h"

// OpenGL only supports a maximum of 8 lights without using extensions.
// I'm not sure if the specs were changed in OGL 2.0
const size_t MAX_LIGHTS = 4;

//intensity distribution of the spotlight with GL_SPOT_EXPONENT

struct Light {
	bool enabled;	// Is the light on/off?
	bool relative;	// Is the light relative to the model? yes/no
	unsigned short type;	// type: 0 = positional, 1 = spot, 2 = directional

	float arc;		// The arc angle of degrees for the light

	float constant_int; // The intensity of the light/colour, also affects the 'focus' of the light. 0.0 being constant (even) lighting
	float linear_int; // light linear quadradic
	float quadradic_int; // light intensity quadradic

	Vec4D pos;		// the position, positional (w > 0) or directional (w = 0)
	Vec4D target;	// the position the lighting is directed at.
	//Vec3D colour;	// not needed?
	Vec4D diffuse;	// The colour
	Vec4D ambience;	// the colour of the ambience
	Vec4D specular;	// colour of specular lighting
};

class LightControl: public wxWindow
{
	DECLARE_CLASS(LightControl)
    DECLARE_EVENT_TABLE()

	//GUI objects
	//wxStaticText *lblCol
	wxStaticText *lblDiff, *lblAmb, *lblSpec;
	wxStaticText *lblPos, *lblIntensity, *lblTar, *lblAlpha;
	wxTextCtrl *txtPosX, *txtPosY, *txtPosZ;
	wxTextCtrl *txtTarX, *txtTarY, *txtTarZ;
	wxCheckBox *enabled, *relative;
	//wxButton *colour, *update
	wxButton *diffuse, *ambience, *specular, *reset;
	wxComboBox *lightSel;
	wxSlider *cintensity, *lintensity, *qintensity, *alpha;
	wxRadioButton *positional, *spot, *directional;
	
	int activeLight;

	Vec3D DoSetColour(const Vec3D &defColor);
	Vec4D DoSetColour(const Vec4D &defColor);
public:
	
	LightControl(wxWindow* parent, wxWindowID id = wxID_ANY);
	~LightControl();

	Light *lights;

	void Init();
	void Update();
	void UpdateGL();

	void SetColour();
	void SetAmbience();
	void SetDiffuse();
	void SetPos(Vec4D p);
	void SetTarget(Vec4D t);
	void SetSpecular();

	Light GetCurrentLight();
	Vec4D GetCurrentPos();
	Vec4D GetCurrentAmbience();

	// Functions to GUI object events
	void OnButton(wxCommandEvent &event);
	void OnCombo(wxCommandEvent &event);
	void OnText(wxCommandEvent &event);
	void OnCheck(wxCommandEvent &event);
	void OnRadio(wxCommandEvent &event);
	void OnScroll(wxScrollEvent &event);

};

#endif

