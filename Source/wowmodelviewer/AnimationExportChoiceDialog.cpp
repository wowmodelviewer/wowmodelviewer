/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * AnimationExportChoiceDialog.cpp
 *
 *  Created on: 3 jul. 2015
 *   Copyright: 2015, WoW Model Viewer (http://wowmodelviewer.net)
 */

#include "AnimationExportChoiceDialog.h"
#include <wx/valtext.h>
#include <algorithm>
#include <climits>

AnimationExportChoiceDialog::AnimationExportChoiceDialog(wxWindow *parent, const wxString &message,
    const wxString &caption, const wxArrayString &choices, const wxArrayInt &animationIds)
  : wxDialog(parent, wxID_ANY, caption, wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_animationIds(animationIds), m_names(choices), m_checked(choices.GetCount(), true)
{
  wxASSERT_MSG(choices.GetCount() == animationIds.GetCount(), "Each animation needs an ID");
  const int inset = FromDIP(14), gap = FromDIP(6);
  auto root = new wxBoxSizer(wxVERTICAL);
  auto content = new wxBoxSizer(wxVERTICAL);
  auto options = new wxBoxSizer(wxHORIZONTAL);
  m_cbMesh = new wxCheckBox(this, wxID_ANY, _("Export Mesh"));
  m_cbSkeleton = new wxCheckBox(this, wxID_ANY, _("Export Skeleton"));
  m_cbSkinning = new wxCheckBox(this, wxID_ANY, _("Export Skinning"));
  m_cbAnimations = new wxCheckBox(this, wxID_ANY, _("Export Animations"));
  for (auto box : {m_cbMesh, m_cbSkeleton, m_cbSkinning, m_cbAnimations})
  {
    box->SetValue(true);
    options->Add(box, 0, wxRIGHT, FromDIP(8));
  }
  content->Add(options, 0, wxBOTTOM, gap);
  content->Add(new wxStaticText(this, wxID_ANY,
      message.IsEmpty() ? _("Select animations you want to export") : message), 0, wxBOTTOM, gap);

  auto selection = new wxBoxSizer(wxHORIZONTAL);
  m_selectall = new wxButton(this, wxID_ANY, _("Select all"));
  m_unselectall = new wxButton(this, wxID_ANY, _("Unselect all"));
  selection->Add(m_selectall, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
  selection->Add(m_unselectall, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
  m_rangeLabel = new wxStaticText(this, wxID_ANY, _("ID range:"));
  const auto field = [this](const wxString &value) {
    auto text = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
                              wxTE_RIGHT, wxTextValidator(wxFILTER_DIGITS));
    // Match the native text field and row sizing used by the URL importer.
    text->SetMinSize(FromDIP(wxSize(42, 10)));
    return text;
  };
  m_rangeFrom = field(wxT("0"));
  m_rangeTo = field(wxT("225"));
  m_rangeFrom->SetToolTip(_("From"));
  m_rangeTo->SetToolTip(_("To"));
  m_rangeFrom->SetName(_("From ID"));
  m_rangeTo->SetName(_("To ID"));
  m_rangeSeparator = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("\xE2\x80\x93"));
  m_selectRange = new wxButton(this, wxID_ANY, _("Select"));
  selection->Add(m_rangeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
  selection->Add(m_rangeFrom, 0, wxEXPAND);
  selection->Add(m_rangeSeparator, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));
  selection->Add(m_rangeTo, 0, wxEXPAND);
  selection->Add(m_selectRange, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
  content->Add(selection, 0, wxBOTTOM, gap);

  auto searchRow = new wxBoxSizer(wxHORIZONTAL);
  m_search = new wxSearchCtrl(this, wxID_ANY);
  m_search->SetDescriptiveText(_("Filter animations"));
  m_search->ShowCancelButton(true);
  m_count = new wxStaticText(this, wxID_ANY, wxEmptyString);
  searchRow->Add(m_search, 1, wxEXPAND | wxRIGHT, gap);
  searchRow->Add(m_count, 0, wxALIGN_CENTER_VERTICAL);
  content->Add(searchRow, 0, wxEXPAND | wxBOTTOM, gap);

  m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(440, 205)),
                         wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_THEME);
  m_list->SetMinSize(FromDIP(wxSize(440, 205)));
  m_list->EnableCheckBoxes();
  m_list->AppendColumn(_("Name"), wxLIST_FORMAT_LEFT, FromDIP(350));
  m_list->AppendColumn(_("ID"), wxLIST_FORMAT_RIGHT, FromDIP(65));
  content->Add(m_list, 1, wxEXPAND);
  m_selectedCount = new wxStaticText(this, wxID_ANY, wxEmptyString);
  content->Add(m_selectedCount, 0, wxEXPAND | wxTOP, gap);
  root->Add(content, 1, wxEXPAND | wxALL, inset);
  root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, inset);
  SetSizer(root);

  m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { RefreshList(); });
  m_search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &) {
    m_search->ChangeValue(wxEmptyString);
    RefreshList();
  });
  m_list->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent &event) {
    const int column = event.GetColumn();
    if (column < 0 || column > 1) return;
    m_sortAscending = column == m_sortColumn ? !m_sortAscending : true;
    m_sortColumn = column;
    RefreshList();
  });
  const auto onCheck = [this](wxListEvent &event) {
    const long row = event.GetIndex();
    if (m_refreshing || row < 0 || row >= (long)m_rows.size()) return;
    m_checked[m_rows[row]] = m_list->IsItemChecked(row);
    UpdateControls();
  };
  m_list->Bind(wxEVT_LIST_ITEM_CHECKED, onCheck);
  m_list->Bind(wxEVT_LIST_ITEM_UNCHECKED, onCheck);
  m_list->Bind(wxEVT_SIZE, [this](wxSizeEvent &event) { FitColumns(); event.Skip(); });
  m_selectall->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    std::fill(m_checked.begin(), m_checked.end(), true);
    RefreshList();
  });
  m_unselectall->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    std::fill(m_checked.begin(), m_checked.end(), false);
    RefreshList();
  });
  m_selectRange->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    long from, to;
    if (!m_cbAnimations->GetValue() || !ReadRange(from, to)) return;
    for (size_t i = 0; i < m_checked.size(); ++i)
      m_checked[i] = m_animationIds[i] >= from && m_animationIds[i] <= to;
    RefreshList();
  });
  for (auto text : {m_rangeFrom, m_rangeTo})
    text->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { UpdateControls(); });
  m_cbAnimations->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { UpdateControls(); });

  RefreshList();
  root->SetSizeHints(this);
  root->Fit(this);
  FitColumns();
  CentreOnParent();
}

bool AnimationExportChoiceDialog::ReadRange(long &from, long &to) const
{
  return m_animationIds.GetCount() == m_checked.size() &&
         m_rangeFrom->GetValue().ToLong(&from) && m_rangeTo->GetValue().ToLong(&to) &&
         from >= 0 && from <= to && to <= INT_MAX;
}

void AnimationExportChoiceDialog::FitColumns()
{
  const int width = m_list->GetClientSize().x - m_list->GetColumnWidth(1) -
                    wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this) - FromDIP(2);
  if (width > FromDIP(40)) m_list->SetColumnWidth(0, width);
}

void AnimationExportChoiceDialog::UpdateControls()
{
  const bool on = m_cbAnimations->GetValue();
  const size_t checked = std::count(m_checked.begin(), m_checked.end(), true);
  m_selectall->Enable(on && checked < m_checked.size());
  m_unselectall->Enable(on && checked > 0);
  m_rangeLabel->Enable(on);
  m_rangeSeparator->Enable(on);
  m_rangeFrom->Enable(on);
  m_rangeTo->Enable(on);
  long from, to;
  m_selectRange->Enable(on && !m_checked.empty() && ReadRange(from, to));
  m_search->Enable(on);
  m_list->Enable(on);
  m_count->Enable(on);
  m_selectedCount->Enable(on);
  m_selectedCount->SetLabel(wxString::Format(_("%u animations selected for export"), (unsigned)checked));
  if (m_rows.size() == m_checked.size())
    m_count->SetLabel(wxString::Format(_("%u animations"), (unsigned)m_checked.size()));
  else
    m_count->SetLabel(wxString::Format(_("%u of %u animations"),
                                     (unsigned)m_rows.size(), (unsigned)m_checked.size()));
  m_count->SetToolTip(wxString::Format(_("%u animations selected for export"), (unsigned)checked));
  Layout();
}

void AnimationExportChoiceDialog::RefreshList()
{
  wxString needle = m_search->GetValue().Trim(true).Trim(false).Lower();
  m_refreshing = true;
  m_list->Freeze();
  m_list->DeleteAllItems();
  m_rows.clear();
  for (size_t i = 0; i < m_checked.size(); ++i)
    if (needle.IsEmpty() || m_names[i].Lower().Contains(needle) ||
        wxString::Format(wxT("%d"), m_animationIds[i]) == needle)
      m_rows.push_back((int)i);
  std::sort(m_rows.begin(), m_rows.end(), [this](int a, int b) {
    const int nameOrder = m_names[a].CmpNoCase(m_names[b]);
    const int idOrder = (m_animationIds[a] > m_animationIds[b]) - (m_animationIds[a] < m_animationIds[b]);
    const int order = m_sortColumn == 0 ? nameOrder : idOrder;
    if (order != 0) return m_sortAscending ? order < 0 : order > 0;
    if (nameOrder != 0) return nameOrder < 0;
    if (idOrder != 0) return idOrder < 0;
    return a < b;
  });
  for (size_t row = 0; row < m_rows.size(); ++row)
  {
    const int clip = m_rows[row];
    m_list->InsertItem((long)row, m_names[clip]);
    m_list->SetItem((long)row, 1, wxString::Format(wxT("%d"), m_animationIds[clip]));
    m_list->CheckItem((long)row, m_checked[clip]);
  }
  m_list->ShowSortIndicator(m_sortColumn, m_sortAscending);
  if (!m_rows.empty()) m_list->EnsureVisible(0);
  m_list->Thaw();
  m_refreshing = false;
  UpdateControls();
  FitColumns();
}

wxArrayInt AnimationExportChoiceDialog::GetAnimationSelections() const
{
  wxArrayInt result;
  for (size_t i = 0; i < m_checked.size(); ++i)
    if (m_checked[i]) result.Add((int)i);
  return result;
}

bool AnimationExportChoiceDialog::exportMesh() const { return m_cbMesh->GetValue(); }
bool AnimationExportChoiceDialog::exportSkeleton() const { return m_cbSkeleton->GetValue(); }
bool AnimationExportChoiceDialog::exportSkinning() const { return m_cbSkinning->GetValue(); }
bool AnimationExportChoiceDialog::exportAnimations() const { return m_cbAnimations->GetValue(); }
