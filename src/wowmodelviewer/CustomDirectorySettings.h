/*
 * CustomDirectorySettings.h
 *
 *  Created on: 11 dec 2015
 *      Author: Jeromnimo
 */

#ifndef _CUSTOMDIRECTORYSETTINGS_H_
#define _CUSTOMDIRECTORYSETTINGS_H_

// WX Headers
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

enum
{
  ID_CUSTOM_DIRECTORY_SETTINGS_APPLY
};

class CustomDirectorySettings: public wxWindow
{
  DECLARE_CLASS(CustomDirectorySettings)
    DECLARE_EVENT_TABLE()

  wxTextCtrl *customDirectoryPathCtrl;

public:

  CustomDirectorySettings(wxWindow* parent, wxWindowID id);
  ~CustomDirectorySettings(){};

  void Update();

  void OnButton(wxCommandEvent &event);
};



#endif /* _CUSTOMDIRECTORYSETTINGS_H_ */
