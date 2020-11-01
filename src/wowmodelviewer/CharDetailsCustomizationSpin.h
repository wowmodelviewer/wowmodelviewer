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

#include "metaclasses/Observer.h"

#include <wx/spinbutt.h>

class CharDetailsCustomizationSpin : public wxWindow, public Observer
{
  public:
  CharDetailsCustomizationSpin(wxWindow* parent, CharDetails & details, CharDetails::CustomizationType type);

  private:
    DECLARE_CLASS(CharDetailsCustomizationSpin)
    DECLARE_EVENT_TABLE()

    void onSpin(wxSpinEvent &event);
    void onEvent(Event *) override;

    void refresh();

    CharDetails::CustomizationType m_type;
    CharDetails & m_details;
    CharDetails::CustomizationParam m_params;

    wxSpinButton * m_spin;
    wxStaticText * m_text;

};


#endif // _CHARDETAILSCUSTOMIZATIONSPIN_H_