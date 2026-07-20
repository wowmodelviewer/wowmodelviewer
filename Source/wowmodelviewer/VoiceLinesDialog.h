/*
 * VoiceLinesDialog.h
 *
 * Creature / Boss VO Browser (V3). Lists a creature model's audio from up to three sources -- Creature
 * Sounds (CreatureSoundData), Creature Voice Lines (sound/creature/<folder>/vo_*.ogg) and Creature Audio
 * Folder (all *.ogg in that folder) -- in a multi-column list (Label / Filename / FileDataID / SoundKitID
 * / Source) with a search/filter box and sort controls, plus Play/Stop, a volume slider (preview only)
 * and Export Original (raw CASC extract). Opened from the AnimControl "Voice Lines..." button; the
 * resolved lines are passed in (AnimControl resolves them via SoundResolver on model change). No quote
 * text is invented and BroadcastText is not used.
 */
#ifndef VOICELINESDIALOG_H
#define VOICELINESDIALOG_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/listctrl.h>

#include <map>
#include <vector>

#include "SoundResolver.h" // VoiceLineEntry

class AudioPlayer;

class VoiceLinesDialog : public wxDialog
{
  DECLARE_EVENT_TABLE()

public:
  VoiceLinesDialog(wxWindow * parent, const wxString & creatureName,
                   const std::vector<VoiceLineEntry> & soundLines,
                   const std::vector<VoiceLineEntry> & voiceFolderLines,
                   const std::vector<VoiceLineEntry> & audioFolderLines,
                   const std::vector<VoiceLineEntry> & encounterLines);
  ~VoiceLinesDialog();

  void OnSource(wxCommandEvent & event);   // switch between Creature Sounds / Voice Lines / Audio Folder
  void OnCategory(wxCommandEvent & event);
  void OnSearch(wxCommandEvent & event);   // filter the current list
  void OnSort(wxCommandEvent & event);     // re-order the current list
  void OnLineActivate(wxListEvent & event); // double-click plays
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
  static wxString BaseName(const QString & path);          // filename component of a listfile path
  static wxString SourceLabel(const QString & source);     // friendly source name
  static wxString StatusLabel(int st);                     // playable/encrypted/missing/unknown
  int entryStatus(int fileDataId);                         // cached GAMEDIRECTORY.fileKeyStatus probe

  wxString                     m_creatureName;
  std::vector<VoiceLineEntry>  m_soundLines;       // V1: Creature Sounds
  std::vector<VoiceLineEntry>  m_voiceFolderLines; // V2: Creature Voice Lines (sound/creature/<f>/vo_*)
  std::vector<VoiceLineEntry>  m_audioFolderLines; // V3: Creature Audio Folder (all *.ogg in the folder)
  std::vector<VoiceLineEntry>  m_encounterLines;   // V3 #4: Encounter Dialogue (boss-named VO folders)
  std::vector<int>             m_filtered;          // list row -> index into the active source list
  std::map<int, int>           m_statusCache;       // FileDataID -> fileKeyStatus (probed once, reused)
  AudioPlayer *                m_player;

  wxChoice *     m_source;
  wxChoice *     m_category;
  wxTextCtrl *   m_search;
  wxChoice *     m_sort;
  wxListCtrl *   m_list;
  wxSlider *     m_volume;
  wxStaticText * m_volumeLabel;
  wxStaticText * m_status;
  wxButton *     m_play;
  wxButton *     m_stop;
  wxButton *     m_export;

  static int s_sessionVolume; // 0..100, remembered across dialog opens for the session
};

#endif // VOICELINESDIALOG_H
