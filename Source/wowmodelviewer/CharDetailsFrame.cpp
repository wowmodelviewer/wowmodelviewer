#include "CharDetailsFrame.h"
#include <wx/sizer.h>
#include "charcontrol.h"
#include "CharDetailsCustomizationChoice.h"
#include "CharDetailsEvent.h"
#include "Game.h"
#include "WoWModel.h"
#include "logger/Logger.h"

IMPLEMENT_CLASS(CharDetailsFrame, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsFrame, wxWindow)
	EVT_BUTTON(wxID_ANY, CharDetailsFrame::onRandomise)
	EVT_CHECKBOX(wxID_ANY, CharDetailsFrame::onDHMode)
END_EVENT_TABLE()

CharDetailsFrame::CharDetailsFrame(wxWindow* parent) : wxWindow(parent, wxID_ANY), model_(nullptr)
{
	LOG_INFO << "Creating CharDetailsFrame...";

	const auto top = new wxFlexGridSizer(1);
	top->AddGrowableCol(0);

	charCustomizationGS_ = new wxFlexGridSizer(1);
	charCustomizationGS_->AddGrowableCol(0);
	top->Add(new wxStaticText(this, -1, _("Model Customization"), wxDefaultPosition, wxSize(-1, 20), wxALIGN_CENTER),
	         wxSizerFlags().Border(wxBOTTOM, 5).Align(wxALIGN_CENTER));

	top->Add(charCustomizationGS_, wxSizerFlags().Border(wxBOTTOM, 5).Expand().Align(wxALIGN_CENTER));
	SetAutoLayout(true);
	top->SetSizeHints(this);
	SetSizer(top);
	wxWindowBase::Layout();
}

void CharDetailsFrame::setModel(WoWModel* model)
{
	if (!model)
		return;

	model_ = model;
	model_->cd.attach(this);

	charCustomizationGS_->Clear(true);

	const auto infos = model_->infos;


	const auto options = GAMEDATABASE.sqlQuery(
		QString(
			"SELECT ID FROM ChrCustomizationOption WHERE ChrModelID = %1 AND ChrCustomizationID != 0 ORDER BY OrderIndex")
		.arg(infos.ChrModelID[0]));

	if (options.valid && !options.values.empty())
	{
		for (auto& option : options.values)
			charCustomizationGS_->Add(new CharDetailsCustomizationChoice(this, model_->cd, option[0].toUInt()),
			                          wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
	}

	SetAutoLayout(true);
	GetSizer()->SetSizeHints(this);
	Layout();
	GetParent()->Layout();
}

void CharDetailsFrame::onRandomise(wxCommandEvent&)
{
	if (!model_)
		return;

	model_->cd.randomise();
}

void CharDetailsFrame::onDHMode(wxCommandEvent& event)
{
	if (!model_)
		return;

	if (event.IsChecked())
		model_->cd.setDemonHunterMode(true);
	else
		model_->cd.setDemonHunterMode(false);

	setModel(model_);
	model_->refresh();
}

void CharDetailsFrame::onEvent(Event* event)
{
	if (event->type() == CharDetailsEvent::DH_MODE_CHANGED)
	{
		dhMode_->SetValue(model_->cd.isDemonHunter());
		setModel(model_);
	}
}
