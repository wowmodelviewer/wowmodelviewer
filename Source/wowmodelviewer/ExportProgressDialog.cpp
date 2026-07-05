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

#include "ExportProgressDialog.h"

#include <wx/gauge.h>
#include <wx/statline.h>

enum
{
  ID_EXPORT_CANCEL = wxID_HIGHEST + 4101
};

BEGIN_EVENT_TABLE(ExportProgressDialog, wxDialog)
  EVT_BUTTON(ID_EXPORT_CANCEL, ExportProgressDialog::onCancel)
  EVT_CLOSE(ExportProgressDialog::onClose)
END_EVENT_TABLE()

ExportProgressDialog::ExportProgressDialog(wxWindow * parent, const wxString & assetLabel,
                                           const wxString & formatLabel)
  : wxDialog(parent, wxID_ANY, wxT("Exporting ") + formatLabel, wxDefaultPosition, wxDefaultSize,
             // No close box: the export must be cancelled via the Cancel button so the
             // child process is actually stopped (a bare close would orphan it).
             wxCAPTION | wxRESIZE_BORDER),
    m_gauge(NULL), m_stageText(NULL), m_clipText(NULL), m_cancelBtn(NULL),
    m_cancelRequested(false)
{
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);

  wxString header = assetLabel.IsEmpty() ? (wxT("Exporting ") + formatLabel + wxT("..."))
                                         : (wxT("Exporting ") + assetLabel + wxT(" to ") + formatLabel + wxT("..."));
  top->Add(new wxStaticText(this, wxID_ANY, header), 0, wxALL, 10);

  m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(360, 18));
  top->Add(m_gauge, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

  m_stageText = new wxStaticText(this, wxID_ANY, wxT("Stage: starting..."));
  top->Add(m_stageText, 0, wxLEFT | wxRIGHT | wxTOP, 10);

  m_clipText = new wxStaticText(this, wxID_ANY, wxT(" "));
  top->Add(m_clipText, 0, wxLEFT | wxRIGHT, 10);

  top->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxALL, 8);

  m_cancelBtn = new wxButton(this, ID_EXPORT_CANCEL, wxT("Cancel"));
  top->Add(m_cancelBtn, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 10);

  SetSizerAndFit(top);
  Centre();
}

void ExportProgressDialog::setStage(const wxString & stage)
{
  if (m_stageText)
    m_stageText->SetLabel(wxT("Stage: ") + stage);
}

void ExportProgressDialog::setClip(int current, int total, const wxString & name)
{
  if (m_clipText)
  {
    if (total > 0)
      m_clipText->SetLabel(wxString::Format(wxT("Animation %d / %d: %s"), current, total, name.c_str()));
    else
      m_clipText->SetLabel(wxT(" "));
  }
}

void ExportProgressDialog::setPercent(int percent)
{
  if (!m_gauge)
    return;
  if (percent < 0)   percent = 0;
  if (percent > 100) percent = 100;
  m_gauge->SetValue(percent);
}

void ExportProgressDialog::markHung(bool hung)
{
  if (!m_stageText)
    return;
  if (hung)
    m_stageText->SetLabel(wxT("Stage: no progress reported (the exporter may be busy or stuck)..."));
}

void ExportProgressDialog::finishMessage(const wxString & msg)
{
  if (m_stageText)
    m_stageText->SetLabel(msg);
}

void ExportProgressDialog::onCancel(wxCommandEvent & WXUNUSED(event))
{
  m_cancelRequested = true;
  if (m_cancelBtn)
  {
    m_cancelBtn->Enable(false);
    m_cancelBtn->SetLabel(wxT("Cancelling..."));
  }
  if (m_stageText)
    m_stageText->SetLabel(wxT("Stage: cancelling export..."));
  // The manager polls cancelRequested(), kills the child, and destroys this dialog
  // from OnTerminate -- we do NOT close here so the child is stopped cleanly first.
}

void ExportProgressDialog::onClose(wxCloseEvent & event)
{
  // Route a window-manager close (e.g. Alt+F4) through the same cancel path instead
  // of letting the dialog vanish while its child process keeps running.
  if (event.CanVeto() && !m_cancelRequested)
  {
    wxCommandEvent dummy;
    onCancel(dummy);
    event.Veto();
    return;
  }
  event.Skip();
}
