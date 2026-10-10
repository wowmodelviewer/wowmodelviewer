/*
 * ClientLoadingPanel.cpp
 */

#include "ClientLoadingPanel.h"

#include <algorithm>

#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/time.h>

#include "ClientInstallations.h"
#include "UiControls.h"
#include "UiStyle.h"
#include "logger/Logger.h"

namespace
{
  wxString wx(const QString & s) { return wxString(s.toStdWString()); }

  // A label's text, wrapped to the page (wxWidgets 3.3 skips a Wrap at the width it last wrapped at, whatever the
  // text: unwrap first).
  void setWrapped(wxStaticText * label, const wxString & text, int width)
  {
    label->SetLabel(text);
    label->Wrap(-1);
    label->Wrap(width);
  }

  // The page repaints and pumps the event loop at most this often: enough for a smooth bar, and a stage that passes
  // in a few milliseconds is never painted (no flicker through the quick ones).
  const long long PUMP_MS = 33;
}

ClientLoadingPanel::ClientLoadingPanel(wxWindow * parent)
  : wxPanel(parent, wxID_ANY)
{
  SetName(wxT("clientLoadingPage"));
  UiStyle::setRole(this, UiStyle::Role::Panel);
  const int L = FromDIP(UiStyle::L), M = FromDIP(UiStyle::M), S = FromDIP(UiStyle::S), XS = FromDIP(UiStyle::XS);
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);

  m_title = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_title->SetName(wxT("loadingTitle"));
  m_title->SetFont(UiStyle::font(UiStyle::Type::Title));
  UiStyle::setRole(m_title, UiStyle::Role::Text);
  top->Add(m_title, 0, wxLEFT | wxRIGHT | wxTOP, L);
  top->AddSpacer(XS);
  m_subtitle = UiStyle::secondaryLabel(this, wxEmptyString);
  top->Add(m_subtitle, 0, wxLEFT | wxRIGHT, L);

  // The progress, under the title: the stage, the bar, a quiet line under it. The page is as tall as what it says
  // (the chooser fits itself to it), so nothing floats in an empty window.
  m_progress = new wxBoxSizer(wxVERTICAL);
  m_stage = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
  m_stage->SetName(wxT("loadingStage"));
  m_stage->SetFont(UiStyle::font(UiStyle::Type::Normal));
  UiStyle::setRole(m_stage, UiStyle::Role::Text);
  m_progress->Add(m_stage, 0, wxEXPAND);
  m_progress->AddSpacer(S);
  m_bar = new UiProgressBar(this);
  m_bar->SetName(wxT("loadingBar"));
  m_progress->Add(m_bar, 0, wxEXPAND);
  m_progress->AddSpacer(S);
  m_detail = UiStyle::secondaryLabel(this, wxEmptyString);
  m_detail->SetName(wxT("loadingDetail"));
  // A line is kept for it from the start (its text comes and goes with the stages), so the window does not grow
  // when it appears.
  m_detail->SetLabel(wxT("Ag"));
  m_detailLine = m_detail->GetBestSize().y;
  m_detail->SetLabel(wxEmptyString);
  m_detail->SetMinSize(wxSize(-1, m_detailLine));
  m_progress->Add(m_detail, 0, wxEXPAND);
  top->Add(m_progress, 0, wxALL | wxEXPAND, L);

  // The failed state, in the progress's place: what went wrong, and the facts.
  m_failure = new wxBoxSizer(wxVERTICAL);
  m_message = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_message->SetName(wxT("loadingMessage"));
  UiStyle::setRole(m_message, UiStyle::Role::Text);
  m_failure->Add(m_message, 0, wxEXPAND);
  m_details = UiStyle::secondaryLabel(this, wxEmptyString);
  m_details->SetName(wxT("loadingDetails"));
  m_failure->Add(m_details, 0, wxTOP | wxEXPAND, L);
  top->Add(m_failure, 0, wxALL | wxEXPAND, L);

  wxPanel * buttons = new wxPanel(this);
  UiStyle::setRole(buttons, UiStyle::Role::Panel);
  wxBoxSizer * column = new wxBoxSizer(wxVERTICAL);
  column->Add(UiStyle::separator(buttons), 0, wxEXPAND);
  wxBoxSizer * row = new wxBoxSizer(wxHORIZONTAL);
  row->AddStretchSpacer();
  m_back = new UiButton(buttons, wxID_ANY, _("Back"), UiButton::Kind::Primary);
  m_back->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { if (onBack) onBack(); });
  row->Add(m_back, 0, wxRIGHT, S);
  UiButton * close = new UiButton(buttons, wxID_ANY, _("Close"), UiButton::Kind::Secondary);
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { if (onClose) onClose(); });
  row->Add(close);
  column->Add(row, 0, wxALL | wxEXPAND, M);
  buttons->SetSizer(column);
  m_buttons = buttons;
  top->Add(buttons, 0, wxEXPAND);

  SetSizer(top);
  top->Show(m_failure, false);
  m_buttons->Hide();
}

void ClientLoadingPanel::begin(const InstalledClient & client)
{
  m_failed = false;
  m_title->SetLabel(wxString::Format(_("Opening %s"), wx(client.profile.friendlyName())));
  m_subtitle->SetLabel(wx(client.profile.versionLabel()));
  m_stage->SetLabel(_("Opening game data..."));
  m_detail->SetLabel(wxEmptyString);
  m_detailText.clear();
  m_detail->SetMinSize(wxSize(-1, m_detailLine));
  m_bar->SetIndeterminate();
  GetSizer()->Show(m_failure, false);
  GetSizer()->Show(m_progress, true);
  m_buttons->Hide();
  Layout();
}

void ClientLoadingPanel::stage(const wxString & text, double fraction)
{
  const bool changed = m_stage->GetLabel() != text;
  if (changed)
  {
    m_stage->SetLabel(text);
    clearDetail();
  }
  if (fraction < 0.0)
    m_bar->SetIndeterminate();
  else
    m_bar->SetValue(fraction);
  pump(changed); // a new stage is on screen before its work starts, however soon after the last repaint it comes
}

void ClientLoadingPanel::progress(double fraction)
{
  if (fraction < 0.0)
    m_bar->SetIndeterminate();
  else
    m_bar->SetValue(fraction);
  pump();
}

void ClientLoadingPanel::detail(const wxString & text)
{
  if (text != m_detailText) // (the label holds the text as wrapped)
  {
    m_detailText = text;
    // Decided on the height the page is laid out for: setting the label has already resized the control to it.
    const int before = m_detail->GetMinSize().y;
    setWrapped(m_detail, text, GetClientSize().x - 2 * FromDIP(UiStyle::L));
    // The line kept for it, or as many as the text takes: the window grows to a longer one.
    m_detail->SetMinSize(wxDefaultSize);
    const int height = std::max(m_detailLine, m_detail->GetBestSize().y);
    m_detail->SetMinSize(wxSize(-1, height));
    if (height != before)
      fit();
    else
      Layout();
  }
  pump();
}

void ClientLoadingPanel::clearDetail()
{
  m_detail->SetLabel(wxEmptyString);
  m_detailText.clear();
  // Back to the one line kept for it: a window grown for a longer one gives the lines back.
  if (m_detail->GetMinSize().y != m_detailLine)
  {
    m_detail->SetMinSize(wxSize(-1, m_detailLine));
    fit();
  }
}

void ClientLoadingPanel::failed(const wxString & title, const wxString & message, const wxString & details)
{
  m_failed = true;
  const int width = GetClientSize().x - 2 * FromDIP(UiStyle::L);
  m_title->SetLabel(title);
  setWrapped(m_message, message, width);
  setWrapped(m_details, details, width);
  GetSizer()->Show(m_progress, false);
  GetSizer()->Show(m_failure, true);
  m_message->Show(!message.IsEmpty());
  m_details->Show(!details.IsEmpty());
  m_buttons->Show();
  fit();
  m_back->SetFocus();
  pump(true);
}

void ClientLoadingPanel::fit()
{
  Layout();
  if (onResize)
    onResize();
}

wxString ClientLoadingPanel::stageText() const
{
  return m_failed ? m_title->GetLabel() : m_stage->GetLabel();
}

void ClientLoadingPanel::pump(bool force)
{
  const long long now = wxGetLocalTimeMillis().GetValue();
  if (!force && now - m_lastPump < PUMP_MS)
    return;
  m_lastPump = now;
  // Painted now, then the event loop runs once: the window keeps answering Windows (it never turns "Not
  // Responding") and the bar's timer moves the slide. The user can reach nothing else meanwhile -- the chooser is
  // modal, its cards are put away, and a second load is refused (ModelViewer::m_clientLoading) -- and the renderer's
  // requests for game files are refused while a client loads (UnityAssetAccess::ClientLoadGuard).
  Refresh(false);
  Update();
  wxYieldIfNeeded();
}
