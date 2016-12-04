/*
* CharDetailsCustomizationSpin.h
*
*  Created on: 4 dec. 2016
*      Author: Jeromnimo
*/

#ifndef _CHARDETAILSCUSTOMIZATIONSPIN_H_
#define _CHARDETAILSCUSTOMIZATIONSPIN_H_

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/window.h>

#include "CharDetails.h"

class wxSpinEvent;

class CharDetails;

class CharDetailsCustomizationSpin : public wxWindow
{
  public:
  CharDetailsCustomizationSpin(wxWindow* parent, CharDetails * details, CharDetails::CustomizationType type);

  private:
    DECLARE_CLASS(CharDetailsCustomizationSpin)
    DECLARE_EVENT_TABLE()

    void onSpin(wxSpinEvent &event);

    CharDetails::CustomizationType m_type;
    std::vector<int> m_values;
};


#endif // _CHARDETAILSCUSTOMIZATIONSPIN_H_