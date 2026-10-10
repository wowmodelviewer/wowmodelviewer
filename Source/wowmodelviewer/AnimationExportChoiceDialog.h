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
 * AnimationExportChoiceDialog.h
 *
 *  Created on: 3 jul. 2015
 *   Copyright: 2015, WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _ANIMATIONEXPORTCHOICEDIALOG_H_
#define _ANIMATIONEXPORTCHOICEDIALOG_H_
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <vector>

class AnimationExportChoiceDialog : public wxDialog
{
public:
  AnimationExportChoiceDialog(wxWindow *parent, const wxString &message, const wxString &caption,
                              const wxArrayString &choices, const wxArrayInt &animationIds);
  bool exportMesh() const;
  bool exportSkeleton() const;
  bool exportSkinning() const;
  bool exportAnimations() const;
  wxArrayInt GetAnimationSelections() const;

private:
  void RefreshList();
  void UpdateControls();
  void FitColumns();
  bool ReadRange(long &from, long &to) const;
  wxArrayInt m_animationIds;
  wxArrayString m_names;
  std::vector<int> m_rows;
  // Indexed by original model position, independently of filtering and sorting.
  std::vector<bool> m_checked;
  int m_sortColumn = 1;
  bool m_sortAscending = true;
  bool m_refreshing = false;
  wxListCtrl *m_list;
  wxSearchCtrl *m_search;
  wxStaticText *m_count;
  wxStaticText *m_selectedCount;
  wxButton *m_selectall, *m_unselectall, *m_selectRange;
  wxStaticText *m_rangeLabel, *m_rangeSeparator;
  wxTextCtrl *m_rangeFrom, *m_rangeTo;
  wxCheckBox *m_cbMesh, *m_cbSkeleton, *m_cbSkinning, *m_cbAnimations;
};
#endif
