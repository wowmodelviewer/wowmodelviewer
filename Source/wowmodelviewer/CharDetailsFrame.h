#pragma once

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

	void setModel(WoWModel* model);

	void onEvent(Event*) override;

private:
	DECLARE_CLASS(CharDetailsFrame)
	DECLARE_EVENT_TABLE()

	wxFlexGridSizer* charCustomizationGS_;
	wxCheckBox* dhMode_;

	void onRandomise(wxCommandEvent& event);
	void onDHMode(wxCommandEvent& event);

	WoWModel* model_;
};
