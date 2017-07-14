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

#include "metaclasses/Observer.h"

class CharDetailsFrame : public wxWindow, public Observer
{
public:
  CharDetailsFrame(wxWindow* parent);

  void randomiseChar();
  void setModel(WoWModel * model);

  virtual void onEvent(Event *);

protected:


private:
  DECLARE_CLASS(CharDetailsFrame)
  DECLARE_EVENT_TABLE()

  wxFlexGridSizer * charCustomizationGS;
  wxCheckBox * dhMode;

  void onRandomise(wxCommandEvent &event);
  void onDHMode(wxCommandEvent &event);

  WoWModel * m_model;
};


#endif /* _CHARDETAILSFRAME_H_ */
