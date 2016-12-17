/*
 * CharDetailsFrame.h
 *
 *  Created on: 21 dec. 2014
 *      Author: Jeromnimo
 */

#ifndef _CHARDETAILSFRAME_H_
#define _CHARDETAILSFRAME_H_

#ifndef WX_PRECOMP
#  include <wx/wx.h>
#endif

#include <wx/window.h>
class wxSpinButton;
class wxSpinEvent;
class wxStaticText;


#include "CharDetails.h"

class CharDetailsFrame : public wxWindow
{
public:
  CharDetailsFrame(wxWindow* parent);

  void randomiseChar();
  void setModel(CharDetails & details);


protected:


private:
  DECLARE_CLASS(CharDetailsFrame)
  DECLARE_EVENT_TABLE()

  wxFlexGridSizer * charCustomizationGS;

  void onRandomise(wxCommandEvent &event);

  CharDetails * m_details;
};


#endif /* _CHARDETAILSFRAME_H_ */
