/*
 * BackgroundColorDialog.h
 *
 * VIEW > SWAP BACKGROUND COLOR: the Models viewport's background (ViewportBackground.h), in a small window of its own
 * that opens at the viewport's top right, clear of the model. A colour picker in the window itself (UiColourPicker: a
 * saturation x brightness square and a hue strip), which the viewport follows while it is dragged and which keeps the
 * colour when it is let go; the colour as #RRGGBB, typed or pasted (applied with Enter or on leaving the field,
 * checked as it is typed, never sent when it is not a colour); the built-in presets and the user's own (saved from
 * the current colour, a duplicate selecting the preset it duplicates; removed by right-click or the Remove button;
 * built-ins cannot be removed); and a reset to the default that keeps the user's presets. Every change shows at once
 * -- there is no Apply -- and only a different colour is sent to the viewport.
 *
 * Modeless, so the model can be turned while colours are tried; Close, Escape or the title bar hide it, and the menu
 * shows it again as it was. A Models command: greyed in the Textures viewer; the window is put away on entering it and
 * given back in Models, as the model panels are (ModelViewer::needsModelViewer, SetViewerMode).
 *
 * The colour itself is ModelViewer's (viewportBackground / setViewportBackground), which shows it, sends it to the
 * player and keeps it; this window edits it and keeps the user's presets. Built from the design system (UiStyle,
 * UiButton, UiColourSwatch, UiColourPicker), so it follows the light and dark themes like the rest of the shell.
 */

#ifndef BACKGROUNDCOLORDIALOG_H
#define BACKGROUNDCOLORDIALOG_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <vector>

#include <wx/timer.h>

class UiButton;
class UiColourPicker;
class UiColourSwatch;
class wxWrapSizer;

class BackgroundColorDialog : public wxDialog
{
public:
  explicit BackgroundColorDialog(wxWindow * parent);
  ~BackgroundColorDialog();

  // Shown from the menu: Undo goes back to the colour on show now, and the HEX field takes the keyboard.
  void opened();

  // The colour changed (here or anywhere else), or the window is shown again: the picker, the chip, the field, the
  // selection marks, the buttons, the hint and what a screen reader says follow it. Not named Update(), which would
  // hide wxWindow::Update() (a repaint).
  void Sync();

  // The first time it is shown: at the top right of the viewport (screen coordinates), clear of the model in its
  // middle. After that it stays wherever the user puts it.
  void placeBeside(const wxRect & viewport);

private:
  wxColour current() const;
  // Lay the panel out again and fit the window to it (a row of presets more or less).
  void relayout();
  // Show colour now (sent only when it differs) and keep it in Config.ini when persist.
  void apply(const wxColour & colour, bool persist);

  // The field.
  void OnHexText(wxCommandEvent & event);
  void OnHexEnter(wxCommandEvent & event);
  void OnHexKillFocus(wxFocusEvent & event);
  // Apply the field's text when it is a colour; otherwise say why on the hint line. True when it was a colour.
  bool commitHex(bool leaving);
  void setHint(const wxString & text, bool warning);
  // The hint for the colour on show: which preset it is, and what a background above #BB does.
  void showColourHint();

  void OnSavePreset(wxCommandEvent & event);
  void OnRemovePreset(wxCommandEvent & event);
  void OnReset(wxCommandEvent & event);

  // The custom presets' swatches, made again from m_customs.
  void rebuildCustomSwatches();
  void removeCustom(size_t index);
  // The index in m_customs of the custom preset with the current colour, or -1.
  int selectedCustom() const;
  void showPresetMenu(UiColourSwatch * swatch, int customIndex);

  wxPanel * m_panel = nullptr;
  UiColourPicker * m_picker = nullptr;
  wxWindow * m_chip = nullptr;   // the colour on show beside its HEX (a ColourChip)
  wxTextCtrl * m_hex = nullptr;
  wxStaticText * m_hint = nullptr;
  UiButton * m_reset = nullptr;
  UiButton * m_undo = nullptr;
  wxColour m_openedWith;     // the colour when the menu last opened the window (Undo)
  UiButton * m_save = nullptr;
  UiButton * m_remove = nullptr;
  UiButton * m_close = nullptr;
  std::vector<UiColourSwatch *> m_builtInSwatches;
  std::vector<UiColourSwatch *> m_customSwatches;
  wxWrapSizer * m_customSizer = nullptr;
  wxStaticText * m_customEmpty = nullptr;
  std::vector<wxColour> m_customs;
  bool m_syncing = false;    // the field is being set from the colour, not typed in
  wxTimer m_previewTimer;    // a drag's newest colour is shown at most every PreviewIntervalMs
  wxColour m_pendingPreview; // ... this one, when the timer fires
  bool m_hexRefused = false; // leaving the field for Save as preset refused what it held (see OnSavePreset)
};

#endif // BACKGROUNDCOLORDIALOG_H
