/*
 * VoiceLinesDialog.h
 *
 * Voice Lines (V1) preview dialog: lists a creature model's resolved sound lines (Aggro/Attack/Wound/
 * Death/Fidget/...), with a category filter, Play/Stop, a volume slider (preview only), and Export
 * Original (raw CASC extract, no transcode). Opened from the AnimControl "Voice Lines..." button. The
 * resolved lines are passed in (AnimControl resolves them via SoundResolver on model change).
 */
#ifndef VOICELINESDIALOG_H
#define VOICELINESDIALOG_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <vector>

#include "SoundResolver.h" // VoiceLineEntry

class AudioPlayer;

class VoiceLinesDialog : public wxDialog
{
  DECLARE_EVENT_TABLE()

public:
  VoiceLinesDialog(wxWindow * parent, const wxString & creatureName,
                   const std::vector<VoiceLineEntry> & soundLines,
                   const std::vector<VoiceLineEntry> & voiceFolderLines);
  ~VoiceLinesDialog();

  void OnSource(wxCommandEvent & event);   // switch between Creature Sounds / Creature Voice Lines
  void OnCategory(wxCommandEvent & event);
  void OnLineActivate(wxCommandEvent & event); // double-click plays
  void OnPlay(wxCommandEvent & event);
  void OnStop(wxCommandEvent & event);
  void OnVolume(wxCommandEvent & event);
  void OnExport(wxCommandEvent & event);
  void OnCloseButton(wxCommandEvent & event);
  void OnClose(wxCloseEvent & event);

private:
  void RebuildCategories();
  void RefillList();
  const std::vector<VoiceLineEntry> & activeLines() const; // the list for the selected source
  const VoiceLineEntry * SelectedEntry() const;
  void SetStatus(const wxString & msg);

  wxString                     m_creatureName;
  std::vector<VoiceLineEntry>  m_soundLines;       // V1: Creature Sounds
  std::vector<VoiceLineEntry>  m_voiceFolderLines; // V2: Creature Voice Lines (sound/creature/<f>/vo_*)
  std::vector<int>             m_filtered;          // listbox row -> index into the active list
  AudioPlayer *                m_player;

  wxChoice *     m_source;
  wxChoice *     m_category;
  wxListBox *    m_list;
  wxSlider *     m_volume;
  wxStaticText * m_volumeLabel;
  wxStaticText * m_status;
  wxButton *     m_play;
  wxButton *     m_stop;
  wxButton *     m_export;

  static int s_sessionVolume; // 0..100, remembered across dialog opens for the session
};

#endif // VOICELINESDIALOG_H
