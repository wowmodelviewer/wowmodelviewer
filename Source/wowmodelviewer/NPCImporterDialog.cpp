/*
 * NPCImporterDialog.cpp
 *
 * See NPCImporterDialog.h.
 */

#include "NPCImporterDialog.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcclient.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "globalvars.h"
#include "ImporterPlugin.h"
#include "ModelIdLookup.h"
#include "modelviewer.h"
#include "NPCInfos.h"
#include "PluginManager.h"
#include "UiControls.h"
#include "UiStyle.h"
#include "UnityAssetAccess.h"

#include "logger/Logger.h"

namespace
{
  // The column the fields and notes span.
  const int ContentWidth = 340;

  // For this run only (not kept on disk), in Load NPC / Model: the page and the ID type last chosen.
  int s_lastPage = 0;   // 0 URL, 1 ID
  int s_lastKind = 0;   // ModelIdLookup::Kind
}

NPCimporterDialog::NPCimporterDialog(wxWindow * parent, Use use)
  : wxDialog(parent, wxID_ANY, use == Use::Load ? _("Load NPC / Model") : _("Import NPC from URL"), wxDefaultPosition,
             wxDefaultSize, wxDEFAULT_DIALOG_STYLE, wxT("npcLoadDialog")),
    m_use(use)
{
  const int xs = FromDIP(UiStyle::XS), s = FromDIP(UiStyle::S), m = FromDIP(UiStyle::M);
  const int width = FromDIP(ContentWidth);
  const int controlHeight = FromDIP(UiStyle::ControlHeight);

  // Everything on one panel in the design system's colours.
  m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, wxT("npcLoadPanel"));
  UiStyle::applyPanel(m_panel);

  // The two ways in, as pages of a book under a tab strip (only the link page for View NPC's import).
  m_book = new wxSimplebook(m_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, wxT("npcLoadBook"));
  UiStyle::applyPanel(m_book);

  // ---- URL
  wxPanel * urlPage = new wxPanel(m_book, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, wxT("npcLoadUrlPage"));
  UiStyle::applyPanel(urlPage);
  wxBoxSizer * urlColumn = new wxBoxSizer(wxVERTICAL);
  urlColumn->Add(new wxStaticText(urlPage, wxID_ANY, _("Wowhead NPC link")), 0);
  m_url = new wxTextCtrl(urlPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(width, controlHeight),
                         wxTE_PROCESS_ENTER);
  m_url->SetName(wxT("npcLoadUrl"));
  UiStyle::setRole(m_url, UiStyle::Role::Field);
  m_url->SetHint(wxT("https://www.wowhead.com/npc=..."));
  urlColumn->Add(m_url, 0, wxEXPAND | wxTOP, xs);
  wxStaticText * urlNote = UiStyle::secondaryLabel(urlPage, _("Reads the NPC and its display ID from the page (needs a "
                                                              "network connection); the model comes from the loaded client."));
  urlNote->Wrap(width);
  urlColumn->Add(urlNote, 0, wxTOP, xs);
  wxBoxSizer * urlOuter = new wxBoxSizer(wxVERTICAL);
  urlOuter->Add(urlColumn, 0, wxLEFT | wxRIGHT | wxTOP, m);
  urlPage->SetSizer(urlOuter);
  m_book->AddPage(urlPage, _("URL"));

  // ---- ID
  if (m_use == Use::Load)
  {
    wxPanel * idPage = new wxPanel(m_book, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, wxT("npcLoadIdPage"));
    UiStyle::applyPanel(idPage);
    wxFlexGridSizer * fields = new wxFlexGridSizer(2, 2, xs, s);
    fields->AddGrowableCol(1, 1);
    // Each label is made just before its control (the order the keyboard and a screen reader follow: the label names
    // the control after it); the grid places them in rows.
    wxStaticText * kindLabel = new wxStaticText(idPage, wxID_ANY, _("ID type"));
    m_kind = new wxChoice(idPage, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(170), controlHeight));
    m_kind->SetName(wxT("npcLoadKind"));
    for (int k = 0; k < ModelIdLookup::KindCount; k++)
      m_kind->Append(ModelIdLookup::label((ModelIdLookup::Kind)k));
    m_kind->SetSelection(s_lastKind);
    wxStaticText * idLabel = new wxStaticText(idPage, wxID_ANY, _("ID"));
    m_id = new wxTextCtrl(idPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, controlHeight), wxTE_PROCESS_ENTER);
    m_id->SetName(wxT("npcLoadId"));
    UiStyle::setRole(m_id, UiStyle::Role::Field);
    m_id->SetHint(wxT("123040"));
    fields->Add(kindLabel);
    fields->Add(idLabel);
    fields->Add(m_kind, 0, wxEXPAND);
    fields->Add(m_id, 1, wxEXPAND);
    // What the selected type loads, so the choice is never a guess about what will show. Set before the dialog is
    // fitted, so it opens at its size, centred.
    m_kindNote = UiStyle::secondaryLabel(idPage, ModelIdLookup::explanation((ModelIdLookup::Kind)s_lastKind));
    m_kindNote->SetName(wxT("npcLoadKindNote"));
    m_kindNote->Wrap(width);
    wxBoxSizer * idColumn = new wxBoxSizer(wxVERTICAL);
    idColumn->SetMinSize(wxSize(width, -1));
    idColumn->Add(fields, 0, wxEXPAND);
    idColumn->Add(m_kindNote, 0, wxEXPAND | wxTOP, xs);
    wxBoxSizer * idOuter = new wxBoxSizer(wxVERTICAL);
    idOuter->Add(idColumn, 0, wxLEFT | wxRIGHT | wxTOP, m);
    idPage->SetSizer(idOuter);
    m_book->AddPage(idPage, _("ID"));
  }

  // The tab strip, edge to edge above the pages (their columns start M in, as its first tab's text does).
  if (m_use == Use::Load)
  {
    m_tabs = new UiTabBar(m_panel, m_book);
    m_tabs->SetName(wxT("npcLoadTabs"));
    m_tabs->SetLabel(_("Load by"));   // what a screen reader calls the strip
    m_tabs->MoveBeforeInTabOrder(m_book);
  }

  // Why Load did nothing, or what it is doing. Three lines are kept for it whatever it says -- the longest message
  // (a Creature Model ID naming a file that cannot be read) takes three -- so the buttons do not move under the
  // pointer (setHint gives a longer one, at a larger text size, the room it needs). Measured as the control measures
  // its own text.
  m_hint = UiStyle::secondaryLabel(m_panel, wxT("X\nX\nX"));
  m_hint->SetName(wxT("npcLoadHint"));
  m_hint->SetMinSize(wxSize(width, m_hint->GetBestSize().y));
  m_hint->SetLabelText(wxT(" "));

  m_cancel = new wxButton(m_panel, wxID_CANCEL, _("Cancel"));
  m_cancel->SetName(wxT("npcLoadCancel"));
  m_load = new wxButton(m_panel, wxID_ANY, m_use == Use::Load ? _("Load") : _("Import"));
  m_load->SetName(wxT("npcLoadLoad"));
  m_load->SetFont(m_load->GetFont().Bold());
  m_load->SetMinSize(wxSize(-1, controlHeight));
  m_load->SetDefault();
  wxBoxSizer * buttons = new wxBoxSizer(wxHORIZONTAL);
  buttons->AddStretchSpacer(1);
  buttons->Add(m_cancel, 0, wxALIGN_CENTER_VERTICAL);
  buttons->Add(m_load, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, s);

  wxBoxSizer * column = new wxBoxSizer(wxVERTICAL);
  if (m_tabs)
    column->Add(m_tabs, 0, wxEXPAND);
  column->Add(m_book, 0, wxEXPAND);
  wxBoxSizer * foot = new wxBoxSizer(wxVERTICAL);
  foot->Add(m_hint, 0, wxEXPAND | wxTOP, s);
  foot->Add(buttons, 0, wxEXPAND | wxTOP, s);
  column->Add(foot, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, m);
  m_panel->SetSizer(column);
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
  top->Add(m_panel, 1, wxEXPAND);
  SetSizerAndFit(top);
  SetEscapeId(wxID_CANCEL);
  CentreOnParent();

  // ---- behaviour
  m_load->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnLoad(); });
  m_url->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { OnLoad(); });
  m_url->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { if (!m_busy) setHint(wxString(), false); });
  if (m_id)
  {
    m_id->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { OnLoad(); });
    m_id->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { if (!m_busy) setHint(wxString(), false); });
    m_kind->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
      s_lastKind = m_kind->GetSelection();
      showKindNote();
      setHint(wxString(), false);
    });
    // The strip keeps the keyboard when it changes the page (UiTabBar: Left / Right go on through the tabs).
    m_book->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent & e) {
      s_lastPage = m_book->GetSelection();
      setHint(wxString(), false);
      e.Skip();
    });
    m_book->SetSelection(s_lastPage);
  }
  // Cancel, Escape and the close box work while a link is being read: the window goes, and what the read brings back
  // when it ends is dropped (importLink). The read cannot be stopped; a page that never answers would otherwise hold the
  // window, and the viewer behind it, for good.
  m_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent & e) {
    if (m_busy)
      m_abandoned = true;
    e.Skip();
  });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent & e) {
    if (m_busy)
      m_abandoned = true;
    e.Skip();
  });
  Bind(wxEVT_INIT_DIALOG, [this](wxInitDialogEvent & e) {
    e.Skip();
    CallAfter([this]() { (idPageShown() ? m_id : m_url)->SetFocus(); });
  });
}

bool NPCimporterDialog::idPageShown() const
{
  return m_id && m_book->GetSelection() == 1;
}

void NPCimporterDialog::showKindNote()
{
  if (!m_kindNote)
    return;
  m_kindNote->SetLabelText(ModelIdLookup::explanation((ModelIdLookup::Kind)m_kind->GetSelection()));
  m_kindNote->Wrap(-1);   // wxWidgets 3.3 skips a Wrap at the width it last wrapped at, whatever the text
  m_kindNote->Wrap(FromDIP(ContentWidth));
  m_panel->Layout();
  Fit();
}

void NPCimporterDialog::setHint(const wxString & text, bool warning)
{
  UiStyle::setRole(m_hint, warning ? UiStyle::Role::WarningText : UiStyle::Role::SecondaryText);
  m_hint->SetLabelText(text.empty() ? wxString(wxT(" ")) : text);
  m_hint->Wrap(-1);   // wxWidgets 3.3 skips a Wrap at the width it last wrapped at, whatever the text
  m_hint->Wrap(FromDIP(ContentWidth));
  // The space kept for it, unless the text needs more (a larger Windows text size): then it gets that, and keeps it,
  // so the window grows once rather than with every message. Measured as the control measures its own text.
  wxClientDC dc(m_hint);
  dc.SetFont(m_hint->GetFont());
  wxCoord textWidth = 0, textHeight = 0;
  dc.GetMultiLineTextExtent(m_hint->GetLabelText(), &textWidth, &textHeight);
  if (textHeight + 1 > m_hint->GetMinSize().y)
  {
    m_hint->SetMinSize(wxSize(FromDIP(ContentWidth), textHeight + 1));
    m_panel->Layout();
    Fit();
  }
  else
    m_panel->Layout();
  m_hint->Refresh();
  m_hint->Update();
}

void NPCimporterDialog::setBusy(bool busy)
{
  m_busy = busy;
  // Cancel stays: see the constructor. The fields go too -- a disabled page does not disable the fields on it.
  m_load->Enable(!busy);
  m_book->Enable(!busy);
  m_url->Enable(!busy);
  if (m_id)
  {
    m_id->Enable(!busy);
    m_kind->Enable(!busy);
  }
  if (m_tabs)
    m_tabs->Enable(!busy);
}

void NPCimporterDialog::OnLoad()
{
  if (m_busy)
    return;
  if (idPageShown() ? loadById() : importLink())
    EndModal(wxID_OK);
}

bool NPCimporterDialog::importLink()
{
  wxString url = m_url->GetValue();
  url.Trim(true).Trim(false);
  if (url.empty())
  {
    setHint(_("Enter a Wowhead NPC link."), true);
    return false;
  }
  const QString link = QString::fromUtf8(url.utf8_str());
  ImporterPlugin * importer = nullptr;
  for (PluginManager::iterator it = PLUGINMANAGER.begin(); it != PLUGINMANAGER.end(); ++it)
  {
    ImporterPlugin * plugin = dynamic_cast<ImporterPlugin *>(*it);
    if (plugin && plugin->acceptURL(link))
      importer = plugin;
  }
  if (!importer)
  {
    setHint(_("That is not a link that can be imported: use a Wowhead NPC page (wowhead.com/npc=...)."), true);
    return false;
  }
  setHint(_("Reading the page..."), false);
  setBusy(true);
  NPCInfos * result = importer->importNPC(link);
  setBusy(false);
  if (m_abandoned)
  {
    // Cancelled while it was read: nothing is loaded.
    LOG_INFO << "NPC link: the read ended after the window was cancelled; dropped.";
    delete result;
    return false;
  }
  if (!result)
  {
    // The importer gives no reason (a page it could not fetch, one without an NPC display, a link it takes but cannot
    // read NPCs from): say what it takes, not a cause.
    setHint(_("No NPC could be read from that link: it has to be a Wowhead NPC page that can be reached."), true);
    return false;
  }
  m_npcId = result->id;
  m_npcDisplayId = result->displayId;
  m_npcType = result->type;
  m_npcName = QString::fromStdWString(result->name);
  delete result;
  LOG_INFO << "NPC link: the page gave NPC" << m_npcId << "display" << m_npcDisplayId << m_npcName;
  return true;
}

bool NPCimporterDialog::loadById()
{
  if (!g_modelViewer || !UnityAssetAccess::hasActiveClient())
  {
    setHint(UnityAssetAccess::isClientLoading() ? _("World of Warcraft is still loading: try again when it is done.")
                                                : _("Load World of Warcraft first: IDs are looked up in the loaded client."),
            true);
    return false;
  }
  int id = 0;
  wxString why;
  if (!ModelIdLookup::parseId(m_id->GetValue(), id, why))
  {
    setHint(why, true);
    return false;
  }
  const ModelIdLookup::Kind kind = (ModelIdLookup::Kind)m_kind->GetSelection();
  ModelIdLookup::Resolved resolved;
  if (!ModelIdLookup::resolve(kind, id, resolved, why))
  {
    // The listfile's path for the file it ended in, which the message leaves out.
    LOG_INFO << "Load NPC / Model by ID:" << QString::fromWCharArray(why.wc_str())
             << (resolved.path.isEmpty() ? QString() : "(" + resolved.path + ")");
    setHint(why, true);
    return false;
  }
  setHint(_("Loading..."), false);
  setBusy(true);
  const bool loaded = g_modelViewer->LoadModelById(resolved, why);
  setBusy(false);
  if (m_abandoned)
    return false;   // cancelled meanwhile (a load that let the window take input): the window is already gone
  if (!loaded)
  {
    setHint(why, true);
    return false;
  }
  return true;
}

int NPCimporterDialog::getImportedId()
{
  return m_npcId;
}

QString NPCimporterDialog::getNPCLine()
{
  if (m_npcId == -1)
    return QString();
  return QString("%1,%2,%3,%4").arg(m_npcId).arg(m_npcDisplayId).arg(m_npcType).arg(m_npcName);
}
