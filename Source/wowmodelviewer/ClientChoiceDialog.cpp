/*
 * ClientChoiceDialog.cpp
 */
#include "ClientChoiceDialog.h"

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/dirdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <QSettings>

#include "UiControls.h"
#include "UiStyle.h"
#include "util.h"             // gamePath, cfgPath
#include "logger/Logger.h"

namespace
{
  wxString wx(const QString & s) { return wxString(s.toStdWString()); }
  // Written as code points: the source is read in the system code page, not UTF-8.
  wxString dot() { return wxString(wxT(" ")) + wxUniChar(0x00B7) + wxT(" "); }
  wxString advancedLabel(bool shown) { return wxString(_("Advanced")) + wxT(" ") + wxUniChar(shown ? 0x25BE : 0x25B8); }
  const int CARD_WIDTH = 440;   // DIP: room for "Classic Era PTR" and "Mists of Pandaria · 5.5.4" on one line each
}

// ------------------------------------------------------------------------------------------------- the card

InstallCard::InstallCard(wxWindow * parent, ClientChoiceDialog * owner, const InstalledClient & client, bool lastUsed,
                         const QString & folderNote)
  : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
    m_owner(owner), m_client(client), m_lastUsed(lastUsed), m_folderNote(folderNote)
{
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  // What a screen reader says for it.
  SetLabel(title() + wxT(", ") + detail() + wxT(", ") + status());
  SetName(title());
  if (openable())
    SetCursor(wxCursor(wxCURSOR_HAND));
  Bind(wxEVT_PAINT, &InstallCard::OnPaint, this);
  Bind(wxEVT_ENTER_WINDOW, &InstallCard::OnMouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &InstallCard::OnMouse, this);
  Bind(wxEVT_LEFT_DOWN, &InstallCard::OnMouse, this);
  Bind(wxEVT_LEFT_UP, &InstallCard::OnMouse, this);
  Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { m_pressed = false; Refresh(); });
  Bind(wxEVT_KEY_DOWN, &InstallCard::OnKey, this);
  Bind(wxEVT_SET_FOCUS, &InstallCard::OnFocus, this);
  Bind(wxEVT_KILL_FOCUS, &InstallCard::OnFocus, this);
}

wxString InstallCard::title() const
{
  return wx(m_client.profile.friendlyName());
}

wxString InstallCard::detail() const
{
  wxString d = wx(m_client.profile.versionLabel());
  if (!m_folderNote.isEmpty())
    d += dot() + wx(m_folderNote);
  return d;
}

wxString InstallCard::status() const
{
  if (m_opening)
    return _("Opening...");
  switch (m_client.data)
  {
    case InstalledClient::Data::Available: return _("Installed");
    case InstalledClient::Data::NotDownloaded: return _("Not downloaded");
    default: return _("Installed");
  }
}

wxString InstallCard::reason() const
{
  return openable() ? wxString()
                    : _("Start it once from Battle.net to download its game data.");
}

void InstallCard::setOpening(bool opening)
{
  m_opening = opening;
  Refresh();
  Update();
}

void InstallCard::setHot(bool hot)
{
  m_hot = hot;
  Refresh();
}

wxSize InstallCard::DoGetBestSize() const
{
  wxClientDC dc(const_cast<InstallCard *>(this));
  dc.SetFont(UiStyle::font(UiStyle::Type::Section));
  const int titleH = dc.GetCharHeight();
  dc.SetFont(UiStyle::font(UiStyle::Type::Normal));
  const int lineH = dc.GetCharHeight();
  int h = FromDIP(UiStyle::M) * 2 + titleH + FromDIP(2) + lineH;
  if (!openable())
    h += FromDIP(UiStyle::XS) + lineH;
  return wxSize(FromDIP(CARD_WIDTH), h);
}

void InstallCard::OnPaint(wxPaintEvent &)
{
  wxAutoBufferedPaintDC paint(this);
  const UiStyle::Palette & p = UiStyle::palette();
  // What is behind the card (its parent's background), then the card.
  paint.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
  paint.Clear();
  wxGCDC dc(paint);
  const wxSize size = GetClientSize();
  const bool focused = HasFocus();
  const bool hot = m_hot && openable() && !m_opening;
  const wxColour fill = (m_pressed && openable()) ? p.pressed : hot ? p.hover : p.controlBackground;
  const wxColour edge = (focused || m_opening) ? p.accent : hot ? p.borderStrong : p.border;
  const int edgeWidth = (focused || m_opening) ? FromDIP(2) : 1;
  const double radius = FromDIP(UiStyle::Radius * 2);
  dc.SetBrush(wxBrush(fill));
  dc.SetPen(wxPen(edge, edgeWidth));
  const int inset = edgeWidth / 2;
  dc.DrawRoundedRectangle(inset, inset, size.x - edgeWidth, size.y - edgeWidth, radius);

  const int padX = FromDIP(UiStyle::L), padY = FromDIP(UiStyle::M);
  int y = padY;

  // The status on the right: "Installed", "Not downloaded", "Opening..."; "Last used" as a pill above it.
  dc.SetFont(UiStyle::font(UiStyle::Type::Normal));
  const wxString st = status();
  const wxSize stSize = dc.GetTextExtent(st);
  dc.SetFont(UiStyle::font(UiStyle::Type::Section));
  const int titleH = dc.GetCharHeight();
  dc.SetFont(UiStyle::font(UiStyle::Type::Normal));
  const int lineH = dc.GetCharHeight();
  int rightEdge = size.x - padX;
  if (m_lastUsed && !m_opening)
  {
    const wxString pill = _("Last used");
    const wxSize ps = dc.GetTextExtent(pill);
    const int pw = ps.x + FromDIP(UiStyle::S) * 2, ph = ps.y + FromDIP(2) * 2;
    const int px = rightEdge - pw, py = y + (titleH - ph) / 2;
    dc.SetBrush(wxBrush(p.accent));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRoundedRectangle(px, py, pw, ph, ph / 2.0);
    dc.SetTextForeground(p.textOnAccent);
    dc.DrawText(pill, px + FromDIP(UiStyle::S), py + FromDIP(2));
  }
  const int detailY = y + titleH + FromDIP(2);
  dc.SetTextForeground(m_opening ? p.accent : openable() ? p.textSecondary : p.warning);
  dc.DrawText(st, rightEdge - stSize.x, detailY + (lineH - stSize.y) / 2);

  // The name, then the expansion and version.
  dc.SetFont(UiStyle::font(UiStyle::Type::Section));
  dc.SetTextForeground(openable() ? p.text : p.textSecondary);
  dc.DrawText(title(), padX, y);
  dc.SetFont(UiStyle::font(UiStyle::Type::Normal));
  dc.SetTextForeground(p.textSecondary);
  const int detailRoom = rightEdge - stSize.x - FromDIP(UiStyle::M) - padX;
  dc.DrawText(wxControl::Ellipsize(detail(), dc, wxELLIPSIZE_END, detailRoom), padX, detailY);
  if (!openable())
  {
    dc.SetTextForeground(p.textSecondary);
    dc.DrawText(wxControl::Ellipsize(reason(), dc, wxELLIPSIZE_END, size.x - padX * 2), padX,
                detailY + lineH + FromDIP(UiStyle::XS));
  }
}

void InstallCard::OnMouse(wxMouseEvent & event)
{
  if (event.Entering())
    m_hot = true;
  else if (event.Leaving())
    m_hot = false;
  else if (event.LeftDown())
  {
    SetFocus();
    if (openable())
    {
      m_pressed = true;
      CaptureMouse();
    }
  }
  else if (event.LeftUp())
  {
    const bool was = m_pressed;
    m_pressed = false;
    if (HasCapture())
      ReleaseMouse();
    if (was && GetClientRect().Contains(event.GetPosition()))
      m_owner->openCard(this);
  }
  Refresh();
  event.Skip();
}

void InstallCard::OnKey(wxKeyEvent & event)
{
  switch (event.GetKeyCode())
  {
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
    case WXK_SPACE:
      m_owner->openCard(this);
      return;
    case WXK_UP:
    case WXK_NUMPAD_UP:     // the keypad's arrows (NumLock off) come as their own codes
      m_owner->focusNeighbour(this, -1);
      return;
    case WXK_DOWN:
    case WXK_NUMPAD_DOWN:
      m_owner->focusNeighbour(this, +1);
      return;
    case WXK_TAB:
      Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
      return;
  }
  event.Skip();
}

void InstallCard::OnFocus(wxFocusEvent & event)
{
  if (event.GetEventType() == wxEVT_SET_FOCUS)
    m_owner->showDetails(this);
  Refresh();
  event.Skip();
}

// ----------------------------------------------------------------------------------------------- the dialog

ClientChoiceDialog::ClientChoiceDialog(wxWindow * parent)
  : wxDialog(parent, wxID_ANY, _("Choose World of Warcraft"), wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE)
{
  UiStyle::setRole(this, UiStyle::Role::Panel);
  buildUI();
  populate(ClientInstallations::candidateRoots());
  CentreOnParent();
}

void ClientChoiceDialog::buildUI()
{
  const int L = FromDIP(UiStyle::L), M = FromDIP(UiStyle::M), S = FromDIP(UiStyle::S);
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);

  wxStaticText * heading = new wxStaticText(this, wxID_ANY, _("Choose World of Warcraft"));
  heading->SetName(wxT("chooserHeading"));
  heading->SetFont(UiStyle::font(UiStyle::Type::Title));
  UiStyle::setRole(heading, UiStyle::Role::Text);
  top->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, L);
  top->AddSpacer(FromDIP(UiStyle::XS));
  top->Add(UiStyle::secondaryLabel(this, _("Select the installation to open.")), 0, wxLEFT | wxRIGHT, L);
  top->AddSpacer(M);

  m_cardsPanel = new wxPanel(this);
  UiStyle::setRole(m_cardsPanel, UiStyle::Role::Panel);
  m_cardsSizer = new wxBoxSizer(wxVERTICAL);
  m_cardsPanel->SetSizer(m_cardsSizer);
  top->Add(m_cardsPanel, 0, wxLEFT | wxRIGHT | wxEXPAND, L);

  m_message = UiStyle::secondaryLabel(this, wxEmptyString);
  m_message->Hide();
  top->Add(m_message, 0, wxLEFT | wxRIGHT | wxTOP, L);

  // The less common ways in, quieter than the cards.
  top->AddSpacer(M);
  UiButton * browse = new UiButton(this, wxID_ANY, _("Browse for another installation..."), UiButton::Kind::Subtle);
  browse->Bind(wxEVT_BUTTON, &ClientChoiceDialog::onBrowse, this);
  top->Add(browse, 0, wxLEFT | wxRIGHT, L - S);
  UiButton * legacy = new UiButton(this, wxID_ANY, _("Open legacy installation..."), UiButton::Kind::Subtle);
  legacy->Bind(wxEVT_BUTTON, &ClientChoiceDialog::onLegacy, this);
  top->Add(legacy, 0, wxLEFT | wxRIGHT | wxTOP, L - S);
  top->Add(UiStyle::secondaryLabel(this, _("An older World of Warcraft installation that uses MPQ archives.")), 0,
           wxLEFT | wxRIGHT, L);

  // Advanced: the technical facts of the card in focus.
  top->AddSpacer(S);
  m_advancedToggle = new UiButton(this, wxID_ANY, advancedLabel(false), UiButton::Kind::Subtle);
  m_advancedToggle->Bind(wxEVT_BUTTON, &ClientChoiceDialog::onAdvanced, this);
  top->Add(m_advancedToggle, 0, wxLEFT | wxRIGHT, L - S);
  m_advancedPanel = new wxPanel(this);
  UiStyle::setRole(m_advancedPanel, UiStyle::Role::Panel);
  m_advancedGrid = new wxFlexGridSizer(2, FromDIP(UiStyle::XS), M);
  m_advancedGrid->AddGrowableCol(1, 1);
  m_advancedPanel->SetSizer(m_advancedGrid);
  m_advancedPanel->Hide();
  top->Add(m_advancedPanel, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, L);

  top->AddSpacer(M);
  top->Add(UiStyle::separator(this), 0, wxEXPAND);
  wxBoxSizer * buttons = new wxBoxSizer(wxHORIZONTAL);
  buttons->AddStretchSpacer();
  buttons->Add(new UiButton(this, wxID_CANCEL, _("Cancel"), UiButton::Kind::Secondary));
  top->Add(buttons, 0, wxALL | wxEXPAND, M);
  SetEscapeId(wxID_CANCEL);
  SetSizer(top);
}

void ClientChoiceDialog::populate(const QStringList & roots)
{
  m_cardsSizer->Clear(true);
  m_cards.clear();
  m_detailsFor = nullptr;

  QSettings settings(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);
  const QString lastRoot = ClientInstallations::rootOf(settings.value("Client/LastPath").toString());
  const QString lastProduct = settings.value("Client/LastProduct").toString();

  wxStopWatch clock;
  QString firstError;
  std::vector<InstalledClient> all;
  for (const QString & root : roots)
  {
    QString error;
    const std::vector<InstalledClient> found = ClientInstallations::discover(root, &error);
    if (found.empty() && firstError.isEmpty())
      firstError = error;
    all.insert(all.end(), found.begin(), found.end());
  }
  // The folder is said only where it is not the main installation's (a second install, e.g. a PTR elsewhere).
  const QString mainRoot = all.empty() ? QString() : all.front().root;
  InstallCard * focus = nullptr;
  for (const InstalledClient & c : all)
  {
    const bool last = !lastProduct.isEmpty() && c.product == lastProduct && c.root.compare(lastRoot, Qt::CaseInsensitive) == 0;
    InstallCard * card = new InstallCard(m_cardsPanel, this, c, last, c.root.compare(mainRoot, Qt::CaseInsensitive) ? c.root : QString());
    if (!m_cards.empty())
      m_cardsSizer->AddSpacer(FromDIP(UiStyle::S));
    m_cardsSizer->Add(card, 0, wxEXPAND);
    m_cards.push_back(card);
    if (last || (!focus && card->openable()))
      focus = last ? card : (focus ? focus : card);
  }
  LOG_INFO << "[clientchooser]" << (int)all.size() << "installations found in" << roots.join(", ") << "in" << clock.Time()
           << "ms";
  for (const InstalledClient & c : all)
    LOG_INFO << "[clientchooser]" << c.profile.friendlyName() << c.product << c.version << c.region << c.locale
             << "| data:" << c.dataDetail;

  if (all.empty())
  {
    m_message->SetLabel(firstError.isEmpty() ? _("No World of Warcraft installation was found. Browse for one below.")
                                             : wx(firstError));
    m_message->Show();
  }
  else
    m_message->Hide();
  relayout();
  if (focus)
    focus->SetFocus();
  else if (!m_cards.empty())
    m_cards.front()->SetFocus();
}

void ClientChoiceDialog::relayout()
{
  m_cardsPanel->Layout();
  Layout();
  GetSizer()->SetSizeHints(this);
  Fit();
}

void ClientChoiceDialog::openCard(InstallCard * card)
{
  if (!card || !m_dataPath.IsEmpty()) // nothing, or a card is already opening (a second Enter before it closed)
    return;
  if (!card->openable())
  {
    // Nothing to open: the card says why; Advanced says the rest.
    showDetails(card);
    wxBell();
    return;
  }
  m_chosen = card->client();
  m_dataPath = wx(m_chosen.dataPath());
  for (InstallCard * c : m_cards)
    c->Enable(c == card);
  card->setOpening(true);
  LOG_INFO << "[clientchooser] opening" << m_chosen.profile.describe() << "from" << m_chosen.root;
  CallAfter([this]() { EndModal(wxID_OK); });
}

void ClientChoiceDialog::focusNeighbour(InstallCard * from, int step)
{
  for (size_t i = 0; i < m_cards.size(); i++)
    if (m_cards[i] == from)
    {
      const int to = (int)i + step;
      if (to >= 0 && to < (int)m_cards.size())
        m_cards[to]->SetFocus();
      return;
    }
}

void ClientChoiceDialog::showDetails(InstallCard * card)
{
  m_detailsFor = card;
  if (!m_advancedShown || !card)
    return;
  m_advancedPanel->Freeze();
  m_advancedGrid->Clear(true);
  const InstalledClient & c = card->client();
  QString schemaHow;
  const QString schema = ClientInstallations::resolveSchema(c.profile, &schemaHow);
  auto row = [this](const wxString & name, const wxString & value) {
    m_advancedGrid->Add(UiStyle::secondaryLabel(m_advancedPanel, name), 0, wxALIGN_TOP);
    wxStaticText * v = new wxStaticText(m_advancedPanel, wxID_ANY, value);
    UiStyle::setRole(v, UiStyle::Role::Text);
    v->Wrap(FromDIP(CARD_WIDTH - 110));
    m_advancedGrid->Add(v, 1, wxEXPAND);
  };
  row(_("Product"), wx(c.product));
  row(_("Version"), wx(c.version));
  row(_("Build"), wxString::Format(wxT("%d"), c.profile.build));
  row(_("Region"), wx(c.region.isEmpty() ? QString("-") : c.region));
  row(_("Language"), wx(c.locale.isEmpty() ? QString("-") : c.locale));
  row(_("Installation"), wx(c.root));
  row(_("Storage"), wx(c.profile.storageName()));
  row(_("Game data"), wx(c.dataDetail));
  row(_("Profile"), wx(schema.isEmpty() ? QString("none") : schemaHow));
  m_advancedPanel->Layout();
  m_advancedPanel->Thaw();
  relayout();
}

void ClientChoiceDialog::setAdvancedShown(bool shown)
{
  m_advancedShown = shown;
  m_advancedToggle->SetLabel(advancedLabel(shown));
  m_advancedPanel->Show(shown);
  if (shown)
    showDetails(m_detailsFor ? m_detailsFor : (m_cards.empty() ? nullptr : m_cards.front()));
  relayout();
}

void ClientChoiceDialog::onAdvanced(wxCommandEvent &)
{
  InstallCard * keep = m_detailsFor;
  setAdvancedShown(!m_advancedShown);
  if (keep)
    keep->SetFocus();
}

void ClientChoiceDialog::onBrowse(wxCommandEvent &)
{
  const wxString start = m_cards.empty() ? wxString() : wx(m_cards.front()->client().root);
  wxDirDialog dlg(this, _("Choose a World of Warcraft installation folder"), start, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
  if (dlg.ShowModal() != wxID_OK)
    return;
  const QString picked = QString::fromWCharArray(dlg.GetPath().wc_str());
  const QString root = ClientInstallations::rootOf(picked);
  if (root.isEmpty())
  {
    m_message->SetLabel(_("No World of Warcraft installation was found in that folder. Choose the folder that "
                          "holds the game (the one with _retail_ or _classic_ in it)."));
    m_message->Show();
    relayout();
    return;
  }
  QStringList roots = ClientInstallations::candidateRoots();
  roots.removeAll(root);
  roots.prepend(root);
  populate(roots);
}

void ClientChoiceDialog::onLegacy(wxCommandEvent &)
{
  m_legacyMpq = true;
  EndModal(wxID_OK);
}
