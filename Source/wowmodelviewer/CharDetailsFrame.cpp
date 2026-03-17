#include "CharDetailsFrame.h"
#include <format>
#include <wx/sizer.h>
#include "charcontrol.h"
#include "CharDetailsCustomizationChoice.h"
#include "CharDetailsEvent.h"
#include "Game.h"
#include "WoWModel.h"
#include "logger/Logger.h"

IMPLEMENT_CLASS(CharDetailsFrame, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsFrame, wxWindow)
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

	top->Add(charCustomizationGS_, wxSizerFlags().Border(wxBOTTOM, 5).Expand());
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
		std::format(
			"SELECT ID FROM ChrCustomizationOption WHERE ChrModelID = {} AND ChrCustomizationID != 0 ORDER BY OrderIndex",
			infos.ChrModelID[0]));

	if (options.valid && !options.values.empty())
	{
		for (auto& option : options.values)
			charCustomizationGS_->Add(new CharDetailsCustomizationChoice(this, model_->cd, std::stoi(option[0])),
			                          wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
	}

	SetAutoLayout(true);
	GetSizer()->SetSizeHints(this);
	Layout();
	GetParent()->Layout();
}

void CharDetailsFrame::onEvent(Event* event)
{
}
