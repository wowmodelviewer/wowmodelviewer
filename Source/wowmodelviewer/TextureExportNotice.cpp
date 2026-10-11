/*
 * TextureExportNotice.cpp -- see TextureExportNotice.h.
 */

#include "TextureExportNotice.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>

#include <wx/dcbuffer.h>

#include "TextureExport.h"
#include "UiControls.h"
#include "UiStyle.h"

// Long enough to read a file name and a folder and reach for Open folder; a failure, which has a reason to
// read, twice as long. Neither counts while the mouse is over the notice.
const int TextureExportNotice::SavedMs = 6000;
const int TextureExportNotice::FailedMs = 12000;

namespace
{
  const int TickMs = 200;
  const int WidthDip = 360;   // fits "Could not save texture" and a folder of a usual depth
  const int MinWidthDip = 220;
  const int InsetDip = 12;    // from the preview's edges, as the preview's own note

  wxString wx(const QString & s) { return wxString(s.toStdWString()); }
}

TextureExportNotice::TextureExportNotice(wxWindow * parent, const wxColour & around)
  : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL, wxT("textureExportNotice")),
    m_around(around)
{
  Hide();
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  UiStyle::setRole(this, UiStyle::Role::Card);   // its labels take the card's colours
  const int xs = FromDIP(UiStyle::XS), s = FromDIP(UiStyle::S), m = FromDIP(UiStyle::M);

  m_title = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                             wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
  m_title->SetName(wxT("textureExportNoticeTitle"));
  m_title->SetFont(UiStyle::font(UiStyle::Type::Strong));
  m_close = new UiButton(this, wxID_ANY, _("Close"), UiButton::Kind::Subtle, UiIcon::Close);
  m_close->SetIconOnly(true);
  m_close->SetCompact(true);
  m_close->SetName(wxT("textureExportNoticeClose"));
  m_close->SetToolTip(_("Close"));
  m_name = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_MIDDLE);
  m_name->SetName(wxT("textureExportNoticeName"));
  UiStyle::setRole(m_name, UiStyle::Role::Text);
  m_folder = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_MIDDLE);
  m_folder->SetName(wxT("textureExportNoticeFolder"));
  UiStyle::setRole(m_folder, UiStyle::Role::SecondaryText);
  m_change = new UiButton(this, wxID_ANY, _("Change folder..."), UiButton::Kind::Secondary);
  m_change->SetCompact(true);
  m_change->SetName(wxT("textureExportNoticeChange"));
  m_change->SetToolTip(_("Choose another texture export folder (Options > Settings > Export)"));
  m_open = new UiButton(this, wxID_ANY, _("Open folder"), UiButton::Kind::Secondary);
  m_open->SetCompact(true);
  m_open->SetName(wxT("textureExportNoticeOpen"));
  m_open->SetToolTip(_("Show the file in Explorer"));
  // The labels are as wide as the notice makes them (ellipsized), not as wide as their text.
  for (wxStaticText * label : { m_title, m_name, m_folder })
    label->SetMinSize(wxSize(1, -1));

  wxBoxSizer * head = new wxBoxSizer(wxHORIZONTAL);
  head->Add(m_title, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, s);
  head->Add(m_close, 0, wxALIGN_CENTER_VERTICAL);
  wxBoxSizer * buttons = new wxBoxSizer(wxHORIZONTAL);
  buttons->AddStretchSpacer();
  buttons->Add(m_change, 0, wxRIGHT, s);
  buttons->Add(m_open, 0);
  wxBoxSizer * body = new wxBoxSizer(wxVERTICAL);
  body->Add(head, 0, wxEXPAND);
  body->Add(m_name, 0, wxEXPAND | wxTOP, xs);
  body->Add(m_folder, 0, wxEXPAND | wxTOP, xs);
  body->Add(buttons, 0, wxEXPAND | wxTOP, s);
  // The text M from the card's left and bottom edges; the close button S from its top-right corner.
  wxBoxSizer * inner = new wxBoxSizer(wxVERTICAL);
  inner->Add(body, 1, wxEXPAND | wxTOP | wxRIGHT, s);
  wxBoxSizer * card = new wxBoxSizer(wxVERTICAL);
  card->Add(inner, 1, wxEXPAND | wxLEFT | wxBOTTOM, m);
  SetSizer(card);

  Bind(wxEVT_PAINT, &TextureExportNotice::OnPaint, this);
  m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { tick(); });
  for (UiButton * button : { m_open, m_change, m_close })
    button->Bind(wxEVT_LEFT_DOWN, &TextureExportNotice::rememberFocus, this);
  m_open->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    TextureExport::showInExplorer(m_file);
    giveBackFocus();
  });
  m_change->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    dismiss();
    if (onChangeFolder)
      onChangeFolder();
  });
  m_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { dismiss(); });
  // Kept in its corner as the preview changes size.
  parent->Bind(wxEVT_SIZE, [this](wxSizeEvent & event) {
    if (IsShown())
      place();
    event.Skip();
  });
}

void TextureExportNotice::showSaved(const QString & file)
{
  m_failed = false;
  m_file = file;
  const QFileInfo info(file);
  m_title->SetLabel(_("Texture saved"));
  UiStyle::setRole(m_title, UiStyle::Role::Text);
  // As they are: SetLabel would take a '&' in a path for a mnemonic.
  m_name->SetLabelText(wx(info.fileName()));
  m_name->SetToolTip(wx(TextureExport::display(info.absoluteFilePath())));
  m_folder->SetLabelText(wx(TextureExport::display(info.absolutePath())));
  m_folder->SetToolTip(wx(TextureExport::display(info.absolutePath())));
  m_open->Show(true);
  m_change->Show(false);
  showNotice(SavedMs);
}

void TextureExportNotice::showFailed(const wxString & reason, const QString & path, bool folderProblem)
{
  m_failed = true;
  m_file = path;
  m_title->SetLabel(_("Could not save texture"));
  UiStyle::setRole(m_title, UiStyle::Role::WarningText);
  m_name->SetLabelText(reason);
  m_name->SetToolTip(reason);
  const wxString shown = path.isEmpty() ? wxString() : wx(TextureExport::display(path));
  m_folder->SetLabelText(shown);
  m_folder->SetToolTip(shown);
  m_open->Show(false);
  m_change->Show(folderProblem);
  showNotice(FailedMs);
}

void TextureExportNotice::showNotice(int ms)
{
  m_remainingMs = ms;
  place();
  if (!IsShown())
    Show();
  Raise();
  Refresh();
  m_timer.Start(TickMs);
}

void TextureExportNotice::dismiss()
{
  m_timer.Stop();
  if (!IsShown())
    return;
  giveBackFocus();
  Hide();
}

void TextureExportNotice::place()
{
  const wxSize area = GetParent()->GetClientSize();
  const int inset = FromDIP(InsetDip);
  const int width = std::max(FromDIP(MinWidthDip), std::min(FromDIP(WidthDip), area.x - 2 * inset));
  GetSizer()->Layout();
  const int height = GetSizer()->CalcMin().y;
  SetSize(area.x - inset - width, area.y - inset - height, width, height);
  Layout();
}

bool TextureExportNotice::hovered() const
{
  return IsShownOnScreen() && GetScreenRect().Contains(wxGetMousePosition());
}

void TextureExportNotice::tick()
{
  // Not on screen (the Textures viewer was left): gone, not waiting there for the next visit.
  if (!IsShownOnScreen())
  {
    dismiss();
    return;
  }
  if (hovered())
    return;   // the user is reading it or reaching for a button
  m_remainingMs -= TickMs;
  if (m_remainingMs <= 0)
    dismiss();
}

void TextureExportNotice::rememberFocus(wxMouseEvent & event)
{
  wxWindow * focus = wxWindow::FindFocus();
  if (focus && focus != this && !IsDescendant(focus))
    m_focusBefore = focus;
  event.Skip();
}

void TextureExportNotice::giveBackFocus()
{
  // Only when a button of the notice has the keyboard: a notice that came and went untouched leaves it alone.
  wxWindow * focus = wxWindow::FindFocus();
  if (!focus || (focus != this && !IsDescendant(focus)))
    return;
  wxWindow * back = m_focusBefore.get();
  if (back && back->IsShownOnScreen() && back->IsEnabled() && !IsDescendant(back))
    back->SetFocus();
  else
    GetParent()->SetFocus();
}

void TextureExportNotice::OnPaint(wxPaintEvent &)
{
  wxAutoBufferedPaintDC dc(this);
  const UiStyle::Palette & p = UiStyle::palette();
  const wxSize size = GetClientSize();
  // The corners outside the rounded card in the colour around it; the card in the field colour with the
  // controls' outline, as the Model panel's cards.
  dc.SetBackground(wxBrush(m_around));
  dc.Clear();
  dc.SetPen(wxPen(p.border));
  dc.SetBrush(wxBrush(p.controlBackground));
  dc.DrawRoundedRectangle(wxRect(size), FromDIP(UiStyle::Radius));
}
