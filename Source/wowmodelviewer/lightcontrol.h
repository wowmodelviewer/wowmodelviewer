#ifndef LIGHTCONTROL_H
#define LIGHTCONTROL_H

// wxWidgets
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif


// OpenGL headers
#include "glm/glm.hpp"

// Vector types
#include "util.h"

// OpenGL only supports a maximum of 8 lights without using extensions.
// I'm not sure if the specs were changed in OGL 2.0
const size_t MAX_LIGHTS = 4;

//intensity distribution of the spotlight with GL_SPOT_EXPONENT

struct Light {
  bool enabled;  // Is the light on/off?
  bool relative;  // Is the light relative to the model? yes/no
  unsigned short type;  // type: 0 = positional, 1 = spot, 2 = directional

  float arc;    // The arc angle of degrees for the light

  float constant_int; // The intensity of the light/colour, also affects the 'focus' of the light. 0.0 being constant (even) lighting
  float linear_int; // light linear quadradic
  float quadradic_int; // light intensity quadradic

  glm::vec4 pos;    // the position, positional (w > 0) or directional (w = 0)
  glm::vec4 target;  // the position the lighting is directed at.
  //glm::vec3 colour;  // not needed?
  glm::vec4 diffuse;  // The colour
  glm::vec4 ambience;  // the colour of the ambience
  glm::vec4 specular;  // colour of specular lighting
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

  glm::vec3 DoSetColour(const glm::vec3 &defColor);
  glm::vec4 DoSetColour(const glm::vec4 &defColor);
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
  void SetPos(glm::vec4 p);
  void SetTarget(glm::vec4 t);
  void SetSpecular();

  Light GetCurrentLight();
  glm::vec4 GetCurrentPos();
  glm::vec4 GetCurrentAmbience();

  // Functions to GUI object events
  void OnButton(wxCommandEvent &event);
  void OnCombo(wxCommandEvent &event);
  void OnText(wxCommandEvent &event);
  void OnCheck(wxCommandEvent &event);
  void OnRadio(wxCommandEvent &event);
  void OnScroll(wxScrollEvent &event);

};

#endif

