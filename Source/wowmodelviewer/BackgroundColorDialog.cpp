/*
 * BackgroundColorDialog.cpp
 *
 * See BackgroundColorDialog.h.
 */

#include "BackgroundColorDialog.h"

#include <algorithm>
#include <memory>

#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/wrapsizer.h>

#include "globalvars.h"
#include "modelviewer.h"
#include "UiControls.h"
#include "UiStyle.h"
#include "ViewportBackground.h"

#include "logger/Logger.h"

namespace
{
  // The colour on show beside its HEX, and a preset swatch. Big enough to judge a colour by (the old customization
  // swatch is 38 x 14), small enough for a row of presets across the window.
  const wxSize CurrentSwatchSize(48, 28);
  const wxSize PresetSwatchSize(34, 34);
  // The window's text column. The picker's colours span it: the picker is laid out Bleed wider on each side, its
  // square's window is the rest after the hue strip, and the square's colours are 160 tall.
  const int ContentWidth = 360;
  const wxSize PickerFieldSize(ContentWidth + 2 * UiColourPicker::Bleed - UiColourPicker::StripWidth - UiColourPicker::StripGap,
                               160 + 2 * UiColourPicker::Bleed);
  // While a drag moves the colour, the viewport is sent the newest at most this often (ms); the last on release.
  const int PreviewIntervalMs = 33;

  // The colour on show, beside its HEX: a chip, not a control (the picker above is how it is chosen).
  class ColourChip : public wxWindow
  {
  public:
    ColourChip(wxWindow * parent, const wxSize & sizeDip) : wxWindow(parent, wxID_ANY), m_sizeDip(sizeDip)
    {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      SetInitialSize();
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (!gc || !m_colour.IsOk())
          return;
        const wxSize size = GetClientSize();
        gc->SetBrush(wxBrush(m_colour));
        gc->SetPen(wxPen(UiStyle::palette().border));
        gc->DrawRoundedRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0, FromDIP(UiStyle::Radius));
      });
    }
    bool AcceptsFocus() const wxOVERRIDE { return false; }
    void SetColour(const wxColour & colour)
    {
      if (colour == m_colour)
        return;
      m_colour = colour;
      Refresh(false);
    }

  protected:
    wxSize DoGetBestSize() const wxOVERRIDE { return FromDIP(m_sizeDip); }

  private:
    wxSize m_sizeDip;
    wxColour m_colour;
  };

  wxString presetLabel(const wxString & name, const wxColour & colour)
  {
    return wxString::Format(wxT("%s  %s"), name, ViewportBackground::formatHex(colour));
  }

  // Only hex digits, an optional leading '#' and spaces around: a colour still being typed.
  bool couldBecomeHex(const wxString & text)
  {
    wxString t = text;
    t.Trim(true).Trim(false);
    if (t.StartsWith(wxT("#")))
      t.Remove(0, 1);
    if (t.length() > 6)
      return false;
    for (wxUniChar c : t)
    {
      const wxChar ch = c;
      if (!((ch >= wxT('0') && ch <= wxT('9')) || (ch >= wxT('a') && ch <= wxT('f')) || (ch >= wxT('A') && ch <= wxT('F'))))
        return false;
    }
    return true;
  }
}

BackgroundColorDialog::BackgroundColorDialog(wxWindow * parent)
  : wxDialog(parent, wxID_ANY, _("Background Color"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE,
             wxT("backgroundColorDialog"))
{
  // Everything on one panel in the design system's colours (the swatches and buttons draw their corners in it).
  m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, wxT("backgroundColorPanel"));
  UiStyle::applyPanel(m_panel);
  const int xs = FromDIP(UiStyle::XS), s = FromDIP(UiStyle::S), m = FromDIP(UiStyle::M), l = FromDIP(UiStyle::L);
  const int width = FromDIP(ContentWidth);
  const wxColour colour = current();

  // A column of a fixed width: the hint and the notes wrap to it, and presets of the user's own wrap onto new rows.
  // The picker sits between its two halves, Bleed wider on each side, so its colours span the column exactly.
  wxBoxSizer * head = new wxBoxSizer(wxVERTICAL);
  head->SetMinSize(wxSize(width, -1));
  wxBoxSizer * column = new wxBoxSizer(wxVERTICAL);
  column->SetMinSize(wxSize(width, -1));

  // ---- The colour.
  head->Add(UiStyle::sectionHeader(m_panel, _("Model viewport background")), 0, wxEXPAND);
  wxStaticText * intro = UiStyle::secondaryLabel(m_panel, _("The color behind the model in the Models viewer. "
                                                         "The Texture Viewer keeps its own backgrounds."));
  intro->Wrap(width);
  head->Add(intro, 0, wxTOP, xs);

  // The picker: drag in the square (saturation across, brightness up) or the hue strip; the viewport follows.
  m_picker = new UiColourPicker(m_panel, wxID_ANY, PickerFieldSize, _("Background color"));
  m_picker->SetName(wxT("viewportBgPicker"));
  m_picker->SetColour(colour);
  const int bleed = FromDIP(UiColourPicker::Bleed);

  wxBoxSizer * row = new wxBoxSizer(wxHORIZONTAL);
  m_chip = new ColourChip(m_panel, CurrentSwatchSize);
  m_chip->SetName(wxT("viewportBgChip"));
  row->Add(m_chip, 0, wxALIGN_CENTER_VERTICAL);

  m_hex = new wxTextCtrl(m_panel, wxID_ANY, ViewportBackground::formatHex(colour), wxDefaultPosition,
                         wxSize(FromDIP(96), -1), wxTE_PROCESS_ENTER);
  m_hex->SetName(wxT("viewportBgHex"));
  UiStyle::setRole(m_hex, UiStyle::Role::Field);
  m_hex->SetToolTip(_("The color as #RRGGBB. Enter applies it."));
  m_hex->SetHint(wxT("#RRGGBB"));
  UiSetAccessibleName(m_hex, _("Background color, hex"));
  row->Add(m_hex, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, s);
  row->AddStretchSpacer(1);

  // Undo: back to the colour this window was opened with (the old dialog's Cancel, for every way the colour moves).
  m_undo = new UiButton(m_panel, wxID_ANY, _("Undo"), UiButton::Kind::Subtle);
  m_undo->SetName(wxT("viewportBgUndo"));
  row->Add(m_undo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, s);

  m_reset = new UiButton(m_panel, wxID_ANY, _("Reset"), UiButton::Kind::Subtle, UiIcon::Reset);
  m_reset->SetName(wxT("viewportBgReset"));
  m_reset->SetToolTip(wxString::Format(_("Back to the default, %s. Your presets stay."),
                                       ViewportBackground::formatHex(ViewportBackground::defaultColour())));
  row->Add(m_reset, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, s);
  column->Add(row, 0, wxEXPAND);

  // Two lines reserved for the hint, the most any hint takes (setHint), so a hint appearing or wrapping moves nothing
  // below it -- not even between a button's press and its release, where a move would lose the click.
  m_hint = UiStyle::secondaryLabel(m_panel, wxT(" "));
  m_hint->SetName(wxT("viewportBgHint"));
  m_hint->SetMinSize(wxSize(width, 2 * m_hint->GetCharHeight()));
  column->Add(m_hint, 0, wxEXPAND | wxTOP, xs);

  // ---- The presets.
  column->Add(UiStyle::sectionHeader(m_panel, _("Presets")), 0, wxEXPAND | wxTOP, l);

  // No wxEXTEND_LAST_ON_EACH_LINE (the default): a swatch keeps its size at the end of a row.
  wxWrapSizer * builtIns = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
  for (const ViewportBackground::Preset & preset : ViewportBackground::builtIns())
  {
    UiColourSwatch * swatch = new UiColourSwatch(m_panel, wxID_ANY, preset.colour, PresetSwatchSize,
                                                 presetLabel(preset.name, preset.colour));
    swatch->SetName(wxString::Format(wxT("viewportBgBuiltIn%d"), (int)m_builtInSwatches.size()));
    swatch->SetToolTip(presetLabel(preset.name, preset.colour));
    const wxColour presetColour = preset.colour;
    swatch->Bind(wxEVT_BUTTON, [this, presetColour](wxCommandEvent &) { apply(presetColour, true); });
    swatch->Bind(wxEVT_CONTEXT_MENU, [this, swatch](wxContextMenuEvent &) { showPresetMenu(swatch, -1); });
    builtIns->Add(swatch, 0, wxRIGHT | wxBOTTOM, xs);
    m_builtInSwatches.push_back(swatch);
  }
  column->Add(builtIns, 0, wxEXPAND | wxTOP, s);

  wxStaticText * yours = UiStyle::secondaryLabel(m_panel, _("Yours"));
  column->Add(yours, 0, wxTOP, s);
  m_customSizer = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
  column->Add(m_customSizer, 0, wxEXPAND | wxTOP, xs);
  m_customEmpty = UiStyle::secondaryLabel(m_panel, _("None yet. Save the color on show to keep it here."));
  m_customEmpty->Wrap(width);
  column->Add(m_customEmpty, 0, wxTOP, xs);

  wxBoxSizer * actions = new wxBoxSizer(wxHORIZONTAL);
  m_save = new UiButton(m_panel, wxID_ANY, _("Save as preset"), UiButton::Kind::Secondary);
  m_save->SetName(wxT("viewportBgSave"));
  m_save->SetToolTip(_("Keep the color on show as a preset of your own"));
  actions->Add(m_save, 0, wxALIGN_CENTER_VERTICAL);
  m_remove = new UiButton(m_panel, wxID_ANY, _("Remove"), UiButton::Kind::Subtle, UiIcon::Close);
  m_remove->SetName(wxT("viewportBgRemove"));
  m_remove->SetToolTip(_("Remove the selected preset of your own (built-in presets stay)"));
  actions->Add(m_remove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, s);
  column->Add(actions, 0, wxTOP, s);

  wxStaticText * how = UiStyle::secondaryLabel(m_panel, _("Right-click a preset of your own to remove it."));
  how->Wrap(width);
  column->Add(how, 0, wxTOP, xs);

  // Close, at the bottom right (Escape too, once the HEX field has nothing of its own to put back).
  m_close = new UiButton(m_panel, wxID_CLOSE, _("Close"), UiButton::Kind::Secondary);
  m_close->SetName(wxT("viewportBgClose"));
  column->Add(m_close, 0, wxALIGN_RIGHT | wxTOP, l);

  wxBoxSizer * outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(head, 0, wxLEFT | wxRIGHT | wxTOP, m);
  outer->AddSpacer(std::max(0, s - bleed));   // the picker's colours an S below the intro ...
  outer->Add(m_picker, 0, wxLEFT | wxRIGHT, std::max(0, m - bleed));
  outer->AddSpacer(std::max(0, s - bleed));   // ... and the HEX row an S below them
  outer->Add(column, 0, wxLEFT | wxRIGHT | wxBOTTOM, m);
  m_panel->SetSizer(outer);
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
  top->Add(m_panel, 1, wxEXPAND);
  SetSizer(top);
  SetEscapeId(wxID_CLOSE);
  // Modeless: closing (Close, Escape, the title bar's X) hides it; View > Swap Background Color shows it again.
  Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Hide(); }, wxID_CLOSE);
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent & e) {
    if (!e.CanVeto())
    {
      e.Skip();
      return;
    }
    e.Veto();
    Hide();
  });

  // A drag shows its colour as it goes (the newest at most every PreviewIntervalMs, nothing kept); letting go keeps it.
  m_previewTimer.SetOwner(this);
  Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
    if (m_pendingPreview.IsOk())
      apply(m_pendingPreview, false);
    m_pendingPreview = wxColour();
  }, m_previewTimer.GetId());
  m_picker->Bind(UI_EVT_COLOUR_CHANGING, [this](wxCommandEvent &) {
    m_pendingPreview = m_picker->GetColour();
    if (!m_previewTimer.IsRunning())
      m_previewTimer.StartOnce(PreviewIntervalMs);
  });
  m_picker->Bind(UI_EVT_COLOUR_CHANGED, [this](wxCommandEvent &) {
    m_previewTimer.Stop();
    m_pendingPreview = wxColour();
    apply(m_picker->GetColour(), true);
  });
  m_hex->Bind(wxEVT_TEXT, &BackgroundColorDialog::OnHexText, this);
  m_hex->Bind(wxEVT_TEXT_ENTER, &BackgroundColorDialog::OnHexEnter, this);
  m_hex->Bind(wxEVT_KILL_FOCUS, &BackgroundColorDialog::OnHexKillFocus, this);
  m_hex->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent & e) {
    // Escape in the field puts the colour on show back instead of closing anything. (Sync leaves a field being typed
    // in alone, so the text is put back here.)
    const wxString hex = ViewportBackground::formatHex(current());
    if (e.GetKeyCode() == WXK_ESCAPE && m_hex->GetValue() != hex)
    {
      m_syncing = true;
      m_hex->ChangeValue(hex);
      m_syncing = false;
      m_hex->SetInsertionPointEnd();
      showColourHint();
      return;
    }
    e.Skip();
  });
  m_save->Bind(wxEVT_BUTTON, &BackgroundColorDialog::OnSavePreset, this);
  m_save->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent & e) {
    m_hexRefused = false;
    e.Skip();
  });
  m_remove->Bind(wxEVT_BUTTON, &BackgroundColorDialog::OnRemovePreset, this);
  m_reset->Bind(wxEVT_BUTTON, &BackgroundColorDialog::OnReset, this);
  m_undo->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    if (m_openedWith.IsOk())
      apply(m_openedWith, true);
  });
  // Hidden (Close, Escape, the title bar, another viewer) with a drag or a held key going on: it ends here, kept,
  // so the viewport and Config.ini agree and nothing goes on moving in a window no one can see.
  Bind(wxEVT_SHOW, [this](wxShowEvent & e) {
    if (!e.IsShown())
      m_picker->settle();
    e.Skip();
  });

  m_customs = ViewportBackground::loadCustomPresets();
  rebuildCustomSwatches();
  Sync();
  CentreOnParent();   // until placeBeside() is told where the viewport is
}

BackgroundColorDialog::~BackgroundColorDialog()
{
  // Nothing may arrive from the picker while this window's members go.
  m_previewTimer.Stop();
  if (m_picker)
    m_picker->abandon();
}

void BackgroundColorDialog::opened()
{
  // Undo goes back to this colour; the HEX field has the keyboard, so a stray arrow key moves nothing.
  m_openedWith = current();
  Sync();
  m_hex->SetFocus();
  m_hex->SelectAll();
}

void BackgroundColorDialog::placeBeside(const wxRect & viewport)
{
  // At the viewport's top right, a margin in, the window's left edge right of the viewport's centre, so the model in
  // the middle stays in view while colours are tried; over the frame's centre when the viewport is too narrow for
  // that. On the viewport's display either way.
  const wxSize size = GetSize();
  const int margin = FromDIP(UiStyle::L);
  if (viewport.width < 2 * (size.x + margin))
    return;
  wxPoint at(viewport.x + viewport.width - margin - size.x, viewport.y + margin);
  const int display = wxDisplay::GetFromPoint(viewport.GetTopLeft());
  if (display != wxNOT_FOUND)
  {
    const wxRect area = wxDisplay(display).GetClientArea();
    at.x = std::max(area.x, std::min(at.x, area.x + area.width - size.x));
    at.y = std::max(area.y, std::min(at.y, area.y + area.height - size.y));
  }
  Move(at);
}

void BackgroundColorDialog::relayout()
{
  // The panel first, then the window to it: presets of the user's own can add a row.
  m_panel->Layout();
  Fit();
  m_panel->Layout();
}

wxColour BackgroundColorDialog::current() const
{
  // The frame owns the colour (the window is made when it is first asked for, so the frame is g_modelViewer by then;
  // the colour it loaded otherwise).
  return g_modelViewer ? g_modelViewer->viewportBackground() : ViewportBackground::loadColour();
}

void BackgroundColorDialog::apply(const wxColour & colour, bool persist)
{
  m_hexRefused = false;
  if (g_modelViewer)
    g_modelViewer->setViewportBackground(colour, persist);   // calls Sync() when the colour changes
  showColourHint();   // and when it does not (the hint may still be about something typed)
}

void BackgroundColorDialog::Sync()
{
  const wxColour colour = current();
  // The picker follows a colour set anywhere else (it keeps the position it was dragged to while dragging).
  m_picker->SetColour(colour);
  static_cast<ColourChip *>(m_chip)->SetColour(colour);
  m_chip->SetToolTip(ViewportBackground::formatHex(colour));
  // The field shows the colour, unless the user is typing in it (a colour applied from elsewhere meanwhile would
  // overwrite what they typed).
  const wxString hex = ViewportBackground::formatHex(colour);
  if (m_hex->GetValue() != hex && !m_hex->HasFocus())
  {
    m_syncing = true;
    m_hex->ChangeValue(hex);
    m_syncing = false;
  }
  const auto & builtIns = ViewportBackground::builtIns();
  for (size_t i = 0; i < m_builtInSwatches.size() && i < builtIns.size(); i++)
    m_builtInSwatches[i]->SetSelected(ViewportBackground::sameColour(builtIns[i].colour, colour));
  for (size_t i = 0; i < m_customSwatches.size() && i < m_customs.size(); i++)
    m_customSwatches[i]->SetSelected(ViewportBackground::sameColour(m_customs[i], colour));
  m_remove->Enable(selectedCustom() >= 0);
  m_reset->Enable(!ViewportBackground::sameColour(colour, ViewportBackground::defaultColour()));
  m_undo->Enable(m_openedWith.IsOk() && !ViewportBackground::sameColour(colour, m_openedWith));
  m_undo->SetToolTip(m_openedWith.IsOk()
                       ? wxString::Format(_("Back to %s, the color when this window was opened"),
                                          ViewportBackground::formatHex(m_openedWith))
                       : wxString());

  // What a screen reader says: the colour on show, and which preset is in use (the ring and check are drawn only).
  const auto name = [](UiColourSwatch * swatch, const wxString & label) {
    if (swatch->GetLabel() != label)
      swatch->SetLabel(label);
  };
  UiSetAccessibleName(m_picker, wxString::Format(_("Background color %s"), hex));
  const wxString selected = _(", selected");
  for (size_t i = 0; i < m_builtInSwatches.size() && i < builtIns.size(); i++)
    name(m_builtInSwatches[i], presetLabel(builtIns[i].name, builtIns[i].colour) +
                               (m_builtInSwatches[i]->IsSelected() ? selected : wxString()));
  for (size_t i = 0; i < m_customSwatches.size() && i < m_customs.size(); i++)
    name(m_customSwatches[i], presetLabel(_("Yours"), m_customs[i]) +
                              (m_customSwatches[i]->IsSelected() ? selected : wxString()));
  showColourHint();
}

void BackgroundColorDialog::setHint(const wxString & text, bool warning)
{
  UiStyle::setRole(m_hint, warning ? UiStyle::Role::WarningText : UiStyle::Role::SecondaryText);
  m_hint->SetLabelText(text.empty() ? wxString(wxT(" ")) : text);
  m_hint->Wrap(FromDIP(ContentWidth));
  m_hint->InvalidateBestSize();
  m_hint->SetMinSize(wxSize(FromDIP(ContentWidth), std::max(2 * m_hint->GetCharHeight(), m_hint->GetBestSize().y)));
  m_panel->Layout();
  m_hint->Refresh();
}

void BackgroundColorDialog::showColourHint()
{
  const wxColour colour = current();
  wxString hint;
  if (const ViewportBackground::Preset * preset = ViewportBackground::builtInFor(colour))
    hint = wxString::Format(_("Built-in preset: %s"), preset->name);
  else if (selectedCustom() >= 0)
    hint = _("Your preset");
  // The viewport's bloom lifts a background whose brightest channel is above #D8 by a step or more (measured; see
  // WmvMain.ApplyViewportBackground): say so where it shows.
  if (colour.Red() > 0xD8 || colour.Green() > 0xD8 || colour.Blue() > 0xD8)
    hint += wxString(hint.empty() ? wxT("") : wxT(". ")) +
            _("Above #D8 the viewport's glow shows the background a little lighter and haloes dark edges.");
  setHint(hint, false);
}

void BackgroundColorDialog::OnHexText(wxCommandEvent & WXUNUSED(event))
{
  if (m_syncing)
    return;
  m_hexRefused = false;
  wxColour colour;
  const wxString text = m_hex->GetValue();
  if (ViewportBackground::parseHex(text, colour))
    setHint(ViewportBackground::sameColour(colour, current()) ? wxString() : _("Press Enter to apply."), false);
  else if (couldBecomeHex(text))
    setHint(_("Six hex digits: #RRGGBB, for example #202428."), false);
  else
    setHint(_("Only 0-9 and A-F: #RRGGBB, for example #202428."), true);
}

bool BackgroundColorDialog::commitHex(bool leaving)
{
  const wxString text = m_hex->GetValue();
  wxColour colour;
  if (ViewportBackground::parseHex(text, colour))
  {
    apply(colour, true);
    // Normalised: "#RRGGBB", upper case, whatever was typed.
    m_syncing = true;
    m_hex->ChangeValue(ViewportBackground::formatHex(colour));
    m_syncing = false;
    return true;
  }
  if (leaving)
  {
    // Not a colour, and the user moved on: the field shows the colour on show again, and says so.
    m_syncing = true;
    m_hex->ChangeValue(ViewportBackground::formatHex(current()));
    m_syncing = false;
    // Quoted short, so the hint stays within its two lines whatever was pasted.
    const wxString shown = text.length() > 16 ? text.Left(15) + wxT("...") : text;
    setHint(wxString::Format(_("\"%s\" is not a color; the background is unchanged."), shown), true);
  }
  else
    setHint(_("Not a color. Type #RRGGBB, for example #202428."), true);
  LOG_INFO << "[viewport] background: not a colour, nothing sent:" << QString::fromWCharArray(text.c_str());
  return false;
}

void BackgroundColorDialog::OnHexEnter(wxCommandEvent & WXUNUSED(event))
{
  commitHex(false);
}

void BackgroundColorDialog::OnHexKillFocus(wxFocusEvent & event)
{
  event.Skip();
  // Going away (the frame closing): nothing to apply, and the frame's state may be going too.
  if (IsBeingDeleted() || m_hex->IsBeingDeleted())
    return;
  const wxString text = m_hex->GetValue();
  // Leaving the field applies what is in it; leaving it as it was changes nothing.
  if (text == ViewportBackground::formatHex(current()))
    return;
  // Switching to another application is not leaving it: the text stays, to finish (or paste into) on the way back.
  wxWindow * to = event.GetWindow();
  if (!to)
    return;
  // A click on something that sets the colour itself (the picker, a preset, Reset) decides the colour: what was typed
  // is put back rather than shown for a moment, sent and kept first. Tab still applies it.
  bool decides = to == m_reset || to == m_picker->Field() || to == m_picker->Strip();
  for (UiColourSwatch * swatch : m_builtInSwatches)
    decides = decides || to == swatch;
  for (UiColourSwatch * swatch : m_customSwatches)
    decides = decides || to == swatch;
  if (decides && wxGetMouseState().LeftIsDown())
  {
    m_syncing = true;
    m_hex->ChangeValue(ViewportBackground::formatHex(current()));
    m_syncing = false;
    return;
  }
  // Refused on the way to Save as preset (a click on it, or Tab to it): that press must not save the colour put back.
  if (!commitHex(true) && event.GetWindow() == m_save)
    m_hexRefused = true;
}

void BackgroundColorDialog::OnSavePreset(wxCommandEvent & WXUNUSED(event))
{
  // The field was refused as the focus came here from it: keep its warning, save nothing (once).
  if (m_hexRefused)
  {
    m_hexRefused = false;
    return;
  }
  // A colour typed but not yet applied is applied first: what is saved is what the field says.
  if (m_hex->GetValue() != ViewportBackground::formatHex(current()) && !commitHex(false))
    return;
  const wxColour colour = current();
  if (const ViewportBackground::Preset * preset = ViewportBackground::builtInFor(colour))
  {
    setHint(wxString::Format(_("Already a preset: %s."), preset->name), false);
    return;
  }
  if (selectedCustom() >= 0)
  {
    setHint(_("Already one of your presets."), false);
    return;
  }
  if (m_customs.size() >= ViewportBackground::MaxCustomPresets)
  {
    setHint(wxString::Format(_("%d presets of your own is the most: remove one first."),
                             (int)ViewportBackground::MaxCustomPresets), true);
    return;
  }
  m_customs.push_back(wxColour(colour.Red(), colour.Green(), colour.Blue()));
  ViewportBackground::saveCustomPresets(m_customs);
  LOG_INFO << "[viewport] background preset saved:" << QString::fromWCharArray(ViewportBackground::formatHex(colour).c_str());
  rebuildCustomSwatches();
  Sync();
}

void BackgroundColorDialog::OnRemovePreset(wxCommandEvent & WXUNUSED(event))
{
  const int index = selectedCustom();
  if (index >= 0)
    removeCustom((size_t)index);
}

void BackgroundColorDialog::removeCustom(size_t index)
{
  if (index >= m_customs.size())
    return;
  LOG_INFO << "[viewport] background preset removed:"
           << QString::fromWCharArray(ViewportBackground::formatHex(m_customs[index]).c_str());
  m_customs.erase(m_customs.begin() + index);
  ViewportBackground::saveCustomPresets(m_customs);
  // The colour on show stays; it is simply no longer a preset.
  rebuildCustomSwatches();
  Sync();
}

void BackgroundColorDialog::OnReset(wxCommandEvent & WXUNUSED(event))
{
  apply(ViewportBackground::defaultColour(), true);
}

int BackgroundColorDialog::selectedCustom() const
{
  const wxColour colour = current();
  for (size_t i = 0; i < m_customs.size(); i++)
    if (ViewportBackground::sameColour(m_customs[i], colour))
      return (int)i;
  return -1;
}

void BackgroundColorDialog::rebuildCustomSwatches()
{
  // The focus may be on a swatch about to go: keep it in the window.
  wxWindow * focus = FindFocus();
  bool focusLost = false;
  for (UiColourSwatch * swatch : m_customSwatches)
  {
    focusLost = focusLost || focus == swatch;
    m_customSizer->Detach(swatch);
    swatch->Destroy();
  }
  m_customSwatches.clear();
  const int xs = FromDIP(UiStyle::XS);
  for (size_t i = 0; i < m_customs.size(); i++)
  {
    const wxColour colour = m_customs[i];
    const wxString label = presetLabel(_("Yours"), colour);
    UiColourSwatch * swatch = new UiColourSwatch(m_panel, wxID_ANY, colour, PresetSwatchSize, label);
    swatch->SetName(wxString::Format(wxT("viewportBgCustom%d"), (int)i));
    swatch->SetToolTip(label + wxT("\n") + _("Right-click to remove"));
    swatch->Bind(wxEVT_BUTTON, [this, colour](wxCommandEvent &) { apply(colour, true); });
    swatch->Bind(wxEVT_CONTEXT_MENU, [this, swatch](wxContextMenuEvent &) {
      for (size_t k = 0; k < m_customSwatches.size(); k++)
        if (m_customSwatches[k] == swatch)
        {
          showPresetMenu(swatch, (int)k);
          break;
        }
    });
    m_customSizer->Add(swatch, 0, wxRIGHT | wxBOTTOM, xs);
    m_customSwatches.push_back(swatch);
  }
  // Tab order: the new swatches after the built-ins, before Save.
  wxWindow * after = m_builtInSwatches.empty() ? (wxWindow *)m_hex : (wxWindow *)m_builtInSwatches.back();
  for (UiColourSwatch * swatch : m_customSwatches)
  {
    swatch->MoveAfterInTabOrder(after);
    after = swatch;
  }
  m_customEmpty->Show(m_customs.empty());
  if (focusLost)
    m_save->SetFocus();
  relayout();
  Refresh();
}

void BackgroundColorDialog::showPresetMenu(UiColourSwatch * swatch, int customIndex)
{
  wxMenu menu;
  const int removeId = wxWindow::NewControlId();
  menu.Append(removeId, customIndex >= 0 ? _("Remove preset") : _("Built-in presets cannot be removed"));
  menu.Enable(removeId, customIndex >= 0);
  const int chosen = swatch->GetPopupMenuSelectionFromUser(menu, wxPoint(0, swatch->GetSize().y));
  wxWindow::UnreserveControlId(removeId);
  // After this handler: removing the preset destroys the swatch whose event this is.
  if (chosen == removeId && customIndex >= 0)
    CallAfter([this, customIndex]() { removeCustom((size_t)customIndex); });
}
