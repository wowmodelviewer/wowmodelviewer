/*
* CharDetailsFrame.cpp
*
*  Created on: 21 dec. 2014
*      Author: Jeromnimo
*/

#include "CharDetailsFrame.h"
#include "UiControls.h"
#include "UiStyle.h"

#include <wx/aui/auibar.h>
#include <wx/sizer.h>

#include "charcontrol.h"
#include "CharDetailsCustomizationChoice.h"
#include "CharDetailsEvent.h"
#include "Game.h"
#include "globalvars.h"
#include "modelviewer.h"
#include "UiArt.h"
#include "WoWModel.h"

#include "logger/Logger.h"


IMPLEMENT_CLASS(CharDetailsFrame, wxWindow)

namespace
{
  enum
  {
    ID_CHAR_MODEL_CLASSIC = wxID_HIGHEST + 6500,
    ID_CHAR_MODEL_HD,
  };
  const wxString CLASSIC_HELP = _("Classic: the original character model");
  const wxString HD_HELP = _("High Definition: the updated character model");
}

BEGIN_EVENT_TABLE(CharDetailsFrame, wxWindow)
EVT_BUTTON(wxID_ANY, CharDetailsFrame::onRandomise)
EVT_CHECKBOX(wxID_ANY, CharDetailsFrame::onDHMode)
END_EVENT_TABLE()


CharDetailsFrame::CharDetailsFrame(wxWindow* parent)
: wxWindow(parent, wxID_ANY), model_(nullptr)
{
  LOG_INFO << "Creating CharDetailsFrame...";
  UiStyle::applyPanel(this);

  auto top = new wxFlexGridSizer(1);
  top->AddGrowableCol(0);

  charCustomizationGS_ = new wxFlexGridSizer(1);
  charCustomizationGS_->AddGrowableCol(0);
  top->Add(UiStyle::sectionHeader(this, _("Character Appearance")), wxSizerFlags().Border(wxBOTTOM, FromDIP(UiStyle::S)).Expand());

  // The character's model of the other generation, where the client ships one (Classic Beta): one segmented control,
  // as the texture viewer's Alpha. Hidden until syncModelVariant says it applies.
  variantBar_ = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT | wxAUI_TB_PLAIN_BACKGROUND | wxAUI_TB_NO_AUTORESIZE);
  variantBar_->SetName(wxT("characterModelVariant"));
  variantBar_->SetArtProvider(new UiToolBarArt(false));
  variantBar_->SetBackgroundColour(UiStyle::palette().panelBackground);
  variantBar_->SetToolBorderPadding(0);
  variantBar_->SetToolPacking(0);
  variantBar_->SetMargins(0, 0, 0, 0);
  variantBar_->AddLabel(wxID_ANY, _("Model"));
  variantBar_->AddTool(ID_CHAR_MODEL_CLASSIC, _("Classic"), wxNullBitmap, CLASSIC_HELP, wxITEM_RADIO);
  variantBar_->AddTool(ID_CHAR_MODEL_HD, _("HD"), wxNullBitmap, HD_HELP, wxITEM_RADIO);
  variantBar_->Realize();
  variantBar_->Bind(wxEVT_TOOL, &CharDetailsFrame::onModelVariant, this);
  top->Add(variantBar_, wxSizerFlags().Border(wxBOTTOM, FromDIP(UiStyle::XS)).Expand());
  variantNote_ = UiStyle::secondaryLabel(this, wxEmptyString);
  top->Add(variantNote_, wxSizerFlags().Border(wxBOTTOM, FromDIP(UiStyle::S)).Expand());
  variantBar_->Hide();
  variantNote_->Hide();
  Bind(wxEVT_SIZE, &CharDetailsFrame::onSize, this);

  top->Add(charCustomizationGS_, wxSizerFlags().Border(wxBOTTOM, 5).Expand());
  auto * row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(new UiButton(this, wxID_ANY, _("Randomise"), UiButton::Kind::Secondary), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
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

  // Night Elf and Blood Elf Demon Hunters -- in a client that has the class (no Classic client does).
  if ((model_->infos.raceID == RACE_NIGHTELF || model_->infos.raceID == RACE_BLOODELF) && model_->cd.clientHasDemonHunters())
    dhMode_->Enable(true);
  else
    dhMode_->Enable(false);

  dhMode_->SetValue(model->cd.isDemonHunter());
  syncModelVariant();
}

void CharDetailsFrame::syncModelVariant()
{
  if (!variantBar_ || !g_modelViewer)
    return;
  const ModelViewer::CharacterVariantState state = g_modelViewer->characterVariantState();
  const int currentId = state.current == CharacterModelVariant::HD ? ID_CHAR_MODEL_HD : ID_CHAR_MODEL_CLASSIC;
  const int otherId = currentId == ID_CHAR_MODEL_HD ? ID_CHAR_MODEL_CLASSIC : ID_CHAR_MODEL_HD;
  const wxString note = !state.shown ? wxString() : (!state.enabled ? state.reason : state.note);
  // The check always follows the model (a refused click leaves the clicked segment checked otherwise); the rest only
  // when it changed. ToggleTool on a radio tool checks it and clears the others, whatever state it is given.
  if (state.shown)
    variantBar_->ToggleTool(currentId, true);
  const wxString signature = wxString::Format(wxT("%d|%d|%d|%d|"), state.shown, state.enabled, (int)state.current, (int)state.other) + note;
  if (signature == variantSignature_)
  {
    variantBar_->Refresh();
    return;
  }
  variantSignature_ = signature;
  variantBar_->EnableTool(currentId, true);
  variantBar_->EnableTool(otherId, state.enabled);
  variantBar_->SetToolShortHelp(ID_CHAR_MODEL_CLASSIC, otherId == ID_CHAR_MODEL_CLASSIC && !state.enabled ? state.reason : CLASSIC_HELP);
  variantBar_->SetToolShortHelp(ID_CHAR_MODEL_HD, otherId == ID_CHAR_MODEL_HD && !state.enabled ? state.reason : HD_HELP);
  variantBar_->Realize();
  variantBar_->Show(state.shown);
  variantNoteText_ = note;
  wrapVariantNote();
  variantNote_->Show(!note.IsEmpty());
  relayoutPage();
}

void CharDetailsFrame::wrapVariantNote()
{
  // Wrapped to the page's width (this panel's own can still be the last layout's), and asking for no width of its own:
  // a long reason never makes the panel wider than the page, which would push the option rows out of sight when the
  // page is narrowed. onSize wraps it again for the page's new width.
  const int pageWidth = GetParent() ? GetParent()->GetClientSize().x : GetClientSize().x;
  const int width = pageWidth - FromDIP(2 * UiStyle::M);
  variantNoteWidth_ = pageWidth;
  variantNote_->SetLabel(variantNoteText_);
  variantNote_->Wrap(-1); // wxWidgets 3.3 skips a Wrap at the width it last wrapped at, whatever the text
  if (!variantNoteText_.IsEmpty())
    variantNote_->Wrap(width > FromDIP(120) ? width : FromDIP(220));
  variantNote_->SetMinSize(wxSize(FromDIP(120), -1));
}

void CharDetailsFrame::relayoutPage()
{
  GetSizer()->SetSizeHints(this);
  Layout();
  GetParent()->Layout();
  if (auto * scrolled = wxDynamicCast(GetParent(), wxScrolledWindow))
    scrolled->FitInside();
}

void CharDetailsFrame::onSize(wxSizeEvent & event)
{
  event.Skip(); // the panel is laid out as before
  const int pageWidth = GetParent() ? GetParent()->GetClientSize().x : GetClientSize().x;
  if (variantNoteText_.IsEmpty() || pageWidth == variantNoteWidth_ || variantRewrapPending_)
    return;
  // After this layout, not inside it.
  variantRewrapPending_ = true;
  CallAfter([this]() {
    variantRewrapPending_ = false;
    wrapVariantNote();
    relayoutPage();
  });
}

void CharDetailsFrame::onModelVariant(wxCommandEvent & event)
{
  const int id = event.GetId();
  if (id != ID_CHAR_MODEL_CLASSIC && id != ID_CHAR_MODEL_HD)
  {
    event.Skip();
    return;
  }
  // From the segment clicked, never from its checked state; after this handler has returned, since the switch
  // rebuilds this panel.
  const CharacterModelVariant target = id == ID_CHAR_MODEL_HD ? CharacterModelVariant::HD : CharacterModelVariant::Classic;
  CallAfter([this, target]() {
    wxString why;
    if (g_modelViewer && !g_modelViewer->SwitchCharacterVariant(target, why) && !why.IsEmpty())
    {
      LOG_INFO << "[variant] switch refused:" << why.ToStdString().c_str();
      g_modelViewer->SetStatusText(why);
    }
    syncModelVariant();
  });
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
    {
      CharDetailsCustomizationChoice * row = new CharDetailsCustomizationChoice(this, model_->cd, option);
      UiStyle::applyPanel(row);   // on the panel's colour, like the rest of the page
      // Its label was made before the row had a colour: the text colour, as every label on the page.
      for (wxWindow * child : row->GetChildren())
        if (wxDynamicCast(child, wxStaticText))
          UiStyle::setRole(child, UiStyle::Role::Text);
      charCustomizationGS_->Add(row, wxSizerFlags(1).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
    }
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

