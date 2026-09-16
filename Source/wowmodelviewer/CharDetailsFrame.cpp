/*
* CharDetailsFrame.cpp
*
*  Created on: 21 dec. 2014
*      Author: Jeromnimo
*/

#include "CharDetailsFrame.h"
#include "UiStyle.h"

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


CharDetailsFrame::CharDetailsFrame(wxWindow* parent)
: wxWindow(parent, wxID_ANY), model_(nullptr)
{
  LOG_INFO << "Creating CharDetailsFrame...";

  auto top = new wxFlexGridSizer(1);
  top->AddGrowableCol(0);

  charCustomizationGS_ = new wxFlexGridSizer(1);
  charCustomizationGS_->AddGrowableCol(0);
  top->Add(UiStyle::sectionHeader(this, _("Customization")), wxSizerFlags().Border(wxBOTTOM, FromDIP(UiStyle::S)).Expand());

  top->Add(charCustomizationGS_, wxSizerFlags().Border(wxBOTTOM, 5).Expand());
  auto * row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(new wxButton(this, wxID_ANY, wxT("Randomise"), wxDefaultPosition, wxDefaultSize), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
  dhMode_ = new wxCheckBox(this, wxID_ANY, wxT("Demon Hunter"), wxDefaultPosition, wxDefaultSize);
  row->Add(dhMode_, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxLEFT, FromDIP(UiStyle::M)));
  top->Add(row, wxSizerFlags().Border(wxTOP, FromDIP(UiStyle::XS)));
  SetAutoLayout(true);
  top->SetSizeHints(this);
  SetSizer(top);
  wxWindowBase::Layout();
}

void CharDetailsFrame::setModel(WoWModel * model)
{
  if (!model)
    return;

  model_ = model;
  model_->cd.attach(this);

  buildRows();

  if (model_->infos.raceID == RACE_NIGHTELF || model_->infos.raceID == RACE_BLOODELF)
    dhMode_->Enable(true);
  else
    dhMode_->Enable(false);

  dhMode_->SetValue(model->cd.isDemonHunter());
}

void CharDetailsFrame::buildRows()
{
  charCustomizationGS_->Clear(true);

  if (model_ && !model_->infos.ChrModelID.empty())
  {
    // One dropdown for each option the character can use with its current choices, in client order:
    // an option whose own requirement fails (the NPC-only "Eye Style" of the classic races) or that
    // has no valid choice gets no row, rather than an empty dropdown. Which options those are can
    // change with a choice (Eyesight goes with Eye Color "Sockets"); see onEvent. Options are no longer
    // filtered by ChrCustomizationID either -- that dropped real options on models mixing tagged and
    // untagged ones (the Dracthyr visage female lost Face, Hair, Horns, Eye Color...).
    for (const uint option : model_->cd.getCustomizationOptions())
      charCustomizationGS_->Add(new CharDetailsCustomizationChoice(this, model_->cd, option), wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
  }

  SetAutoLayout(true);
  GetSizer()->SetSizeHints(this);
  Layout();
  GetParent()->Layout();
  if (auto * scrolled = wxDynamicCast(GetParent(), wxScrolledWindow))
    scrolled->FitInside();
}

void CharDetailsFrame::onRandomise(wxCommandEvent &)
{
  if (!model_)
    return;

  model_->cd.randomise();
}

void CharDetailsFrame::onDHMode(wxCommandEvent &event)
{
  if (!model_)
    return;

  // Re-validates every option for the new class context and refreshes the model once.
  if (event.IsChecked())
    model_->cd.setDemonHunterMode(true);
  else
    model_->cd.setDemonHunterMode(false);

  setModel(model_);
}

void CharDetailsFrame::onEvent(Event * event)
{
  if (event->type() == CharDetailsEvent::DH_MODE_CHANGED)
  {
    dhMode_->SetValue(model_->cd.isDemonHunter());
    setModel(model_);
  }
  else if (event->type() == CharDetailsEvent::OPTION_LIST_CHANGED && model_)
  {
    // The options the character can use changed (e.g. Eyesight after Eye Color "Sockets"). The rows
    // are rebuilt after the current event has been handled: the change usually comes from one of
    // these rows' own dropdown, whose control must not be destroyed while its handler runs.
    if (!rebuildPending_)
    {
      rebuildPending_ = true;
      const WoWModel * model = model_;
      CallAfter([this, model]() {
        rebuildPending_ = false;
        if (model_ == model)
          buildRows();
      });
    }
  }
}

