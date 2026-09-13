/*
 * KeyboardShortcutsDialog.h
 *
 * Help > Keyboard Shortcuts: every shortcut the application actually has, in one list. Menu
 * shortcuts are read from the menu bar itself, so the list cannot drift from the menus; the
 * handful that exist only in the accelerator table, and the viewport's mouse and key controls,
 * are passed in by the caller from the same table that installs them.
 */

#ifndef KEYBOARDSHORTCUTSDIALOG_H
#define KEYBOARDSHORTCUTSDIALOG_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <vector>

struct ShortcutInfo
{
  wxString section;
  wxString keys;
  wxString action;
};

class KeyboardShortcutsDialog : public wxDialog
{
public:
  KeyboardShortcutsDialog(wxWindow * parent, wxMenuBar * menuBar, const std::vector<ShortcutInfo> & extra);
};

#endif // KEYBOARDSHORTCUTSDIALOG_H
