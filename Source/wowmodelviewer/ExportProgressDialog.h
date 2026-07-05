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
 * ExportProgressDialog.h
 *
 *  A small MODELESS progress window shown while an FBX export runs in a separate
 *  process. Being modeless (Show(), never ShowModal()) is the point: the main
 *  application window stays fully interactive while the export proceeds. The
 *  owning ExportJobManager drives it via setStage()/setClip()/setPercent() from
 *  progress lines streamed by the child, and reads cancelRequested() to know when
 *  the user pressed Cancel.
 */

#ifndef _EXPORTPROGRESSDIALOG_H_
#define _EXPORTPROGRESSDIALOG_H_

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

class wxGauge;
class wxStaticText;
class wxButton;

class ExportProgressDialog : public wxDialog
{
  public:
    // formatLabel names the export kind ("FBX", "Image Sequence", ...) shown in the
    // window title and header, so this dialog can front any export, not just FBX.
    ExportProgressDialog(wxWindow * parent, const wxString & assetLabel,
                         const wxString & formatLabel = wxT("FBX"));
    ~ExportProgressDialog() {}

    // Driven by ExportJobManager as progress lines arrive from the child process.
    void setStage(const wxString & stage);
    void setClip(int current, int total, const wxString & name);
    void setPercent(int percent);          // 0..100 (clamped)
    void markHung(bool hung);              // shows a "no progress" hint, keeps Cancel live
    void finishMessage(const wxString & msg); // final line before the dialog is destroyed

    // True once the user pressed Cancel (the button latches and disables itself).
    bool cancelRequested() const { return m_cancelRequested; }

  private:
    void onCancel(wxCommandEvent & event);
    void onClose(wxCloseEvent & event);

    wxGauge      * m_gauge;
    wxStaticText * m_stageText;
    wxStaticText * m_clipText;
    wxButton     * m_cancelBtn;
    bool           m_cancelRequested;

    DECLARE_EVENT_TABLE();
};

#endif /* _EXPORTPROGRESSDIALOG_H_ */
