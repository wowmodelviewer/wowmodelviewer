/*
 * DisplaySettings.h
 *
 *  Created on: 1 may 2015
 *      Author: Jeromnimo
 */

#ifndef _DISPLAYSETTINGS_H_
#define _DISPLAYSETTINGS_H_

// WX Headers
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

enum
{
  ID_DISPLAY_SETTINGS_APPLY,
  ID_CHECK_ENVMAPPING
};

enum {
  CHECK_COMPRESSEDTEX,
  CHECK_MULTITEX,
  CHECK_VBO,
  CHECK_FBO,
  CHECK_PBO,
  CHECK_DRAWRANGEELEMENTS,
  CHECK_ENVMAPPING,
  CHECK_NPOT,
  CHECK_PIXELSHADERS,
  CHECK_VERTEXSHADERS,
  CHECK_GLSLSHADERS,

  NUM_SETTINGS2_CHECK
};

class DisplaySettings: public wxWindow
{
  DECLARE_CLASS(DisplaySettings)
    DECLARE_EVENT_TABLE()

  wxComboBox *oglMode;
  wxCheckBox *chkBox[NUM_SETTINGS2_CHECK];
  wxTextCtrl *txtFov;

public:

  DisplaySettings(wxWindow* parent, wxWindowID id);
  ~DisplaySettings() {};

  void Update();
  void OnButton(wxCommandEvent &event);
  void OnCheck(wxCommandEvent &event);
};



#endif /* _DISPLAYSETTINGS_H_ */
