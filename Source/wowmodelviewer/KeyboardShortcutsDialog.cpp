/*
 * KeyboardShortcutsDialog.cpp
 */

#include "KeyboardShortcutsDialog.h"

#include <wx/listctrl.h>

#include "UiStyle.h"

namespace
{
  void collectMenu(wxMenu * menu, const wxString & path, std::vector<ShortcutInfo> & out)
  {
    if (!menu)
      return;
    for (wxMenuItemList::compatibility_iterator node = menu->GetMenuItems().GetFirst(); node; node = node->GetNext())
    {
      wxMenuItem * item = node->GetData();
      if (item->IsSeparator())
        continue;
      if (item->IsSubMenu())
      {
        collectMenu(item->GetSubMenu(), path + wxT(" > ") + item->GetItemLabelText(), out);
        continue;
      }
      wxString keys = item->GetItemLabel().AfterFirst('\t');
      if (keys.IsEmpty())
        continue;
      // Menu labels spell modifiers in capitals ("CTRL+X"); show them as the rest of the list does.
      keys.Replace(wxT("CTRL"), wxT("Ctrl"));
      keys.Replace(wxT("SHIFT"), wxT("Shift"));
      keys.Replace(wxT("ALT"), wxT("Alt"));
      ShortcutInfo info;
      info.section = path;
      info.keys = keys;
      info.action = item->GetItemLabelText();
      out.push_back(info);
    }
  }
}

KeyboardShortcutsDialog::KeyboardShortcutsDialog(wxWindow * parent, wxMenuBar * menuBar,
                                                 const std::vector<ShortcutInfo> & extra)
  : wxDialog(parent, wxID_ANY, _("Keyboard Shortcuts"), wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
  std::vector<ShortcutInfo> rows;
  if (menuBar)
    for (size_t i = 0; i < menuBar->GetMenuCount(); i++)
      collectMenu(menuBar->GetMenu(i), wxMenuItem::GetLabelText(menuBar->GetMenuLabel(i)), rows);
  rows.insert(rows.end(), extra.begin(), extra.end());

  wxListCtrl * list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(620, 480)),
                                     wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_THEME);
  list->AppendColumn(_("Shortcut"), wxLIST_FORMAT_LEFT, FromDIP(150));
  list->AppendColumn(_("Action"), wxLIST_FORMAT_LEFT, FromDIP(290));
  list->AppendColumn(_("Where"), wxLIST_FORMAT_LEFT, FromDIP(150));

  long row = 0;
  for (const ShortcutInfo & info : rows)
  {
    row = list->InsertItem(row, info.keys);
    list->SetItem(row, 1, info.action);
    list->SetItem(row, 2, info.section);
    row++;
  }

  const int md = FromDIP(UiStyle::M);
  wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(UiStyle::secondaryLabel(this, _("Viewport shortcuts apply while that viewport has the keyboard focus.")),
             0, wxLEFT | wxRIGHT | wxTOP, md);
  sizer->Add(list, 1, wxEXPAND | wxALL, md);
  sizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, md);
  SetSizerAndFit(sizer);
  SetEscapeId(wxID_CLOSE);
  SetAffirmativeId(wxID_CLOSE);
  CentreOnParent();
}
