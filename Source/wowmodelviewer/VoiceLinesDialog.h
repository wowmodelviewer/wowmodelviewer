/*
 * VoiceLinesDialog.h
 *
 * Creature / Boss VO Browser (V3). Lists a creature model's audio from several sources -- Creature
 * Sounds (CreatureSoundData), Creature Voice Lines (sound/creature/<folder>/vo_*.ogg), Creature Audio
 * Folder (all *.ogg), Encounter Dialogue (boss-named VO via JournalEncounter) and an opt-in debug
 * "Candidates" list -- in a multi-column list with search/filter/sort, Play/Stop, a volume slider and
 * OGG export (single, selected, or all-visible). Opened from the AnimControl "Voice Lines" button; the
 * resolved lines are passed in (AnimControl resolves them via SoundResolver on model change).
 *
 * Availability status (playable / encrypted / missing) is probed OFF the open path: rows appear
 * instantly and the Status column fills in via a wxTimer in small batches, so opening never blocks on
 * CASC. No quote text is invented and BroadcastText is not used.
 */
#ifndef VOICELINESDIALOG_H
#define VOICELINESDIALOG_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/listctrl.h>

#include <map>
#include <set>
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
                   const std::vector<VoiceLineEntry> & encounterLines,
                   const std::vector<VoiceLineEntry> & candidateLines);
  ~VoiceLinesDialog();

  void OnSource(wxCommandEvent & event);
  void OnCategory(wxCommandEvent & event);
  void OnSearch(wxCommandEvent & event);
  void OnSort(wxCommandEvent & event);
  void OnLineActivate(wxListEvent & event); // double-click plays
  void OnPlay(wxCommandEvent & event);
  void OnStop(wxCommandEvent & event);
  void OnVolume(wxCommandEvent & event);
  void OnExportSelected(wxCommandEvent & event); // export the selected row(s)
  void OnExportVisible(wxCommandEvent & event);  // export every row currently shown (after filter)
  void OnStatusTimer(wxTimerEvent & event);      // incremental availability probe (off the open path)
  void OnCloseButton(wxCommandEvent & event);
  void OnClose(wxCloseEvent & event);

private:
  void RebuildCategories();
  void RefillList();
  const std::vector<VoiceLineEntry> & activeLines() const;
  const VoiceLineEntry * SelectedEntry() const;
  std::vector<int> selectedLineIndices() const;  // line indices of selected rows (into activeLines)
  std::vector<int> visibleLineIndices() const;   // line indices of all filtered/visible rows
  void SetStatus(const wxString & msg);

  static wxString BaseName(const QString & path);
  static wxString SourceLabel(const QString & source);
  static wxString SourceToken(const QString & source); // short slug for export filenames
  static wxString StatusLabel(int st);                 // playable/encrypted/missing/checking/unknown

  int  probeStatus(int fileDataId);   // BLOCKING fileKeyStatus (timer only), fills the cache
  int  cachedStatus(int fileDataId) const; // NON-blocking: cached value or ST_PENDING
  void startStatusProbe();            // queue uncached ids + kick the timer; toggles export buttons
  void refreshVisibleStatus();        // repaint the Status column from the cache

  // Shared export: write each line to <folder>, tally results, write manifest.csv, show a summary.
  void exportRows(const std::vector<int> & lineIdx);

  wxString                     m_creatureName;
  std::vector<VoiceLineEntry>  m_soundLines;       // V1: Creature Sounds
  std::vector<VoiceLineEntry>  m_voiceFolderLines; // V2: Creature Voice Lines (sound/creature/<f>/vo_*)
  std::vector<VoiceLineEntry>  m_audioFolderLines; // V3: Creature Audio Folder (all *.ogg)
  std::vector<VoiceLineEntry>  m_candidateLines;   // debug-only, unattributed audition list
  std::vector<VoiceLineEntry>  m_encounterLines;   // V3 #4: Encounter Dialogue (boss-named VO folders)
  std::vector<int>             m_filtered;          // list row -> index into the active source list

  std::map<int, int> *         m_statusCache;       // -> persistent per-build FileDataID->status map
  std::vector<int>             m_pending;            // FileDataIDs still to probe (timer drains this)
  wxTimer *                    m_statusTimer;
  size_t                       m_statusTotal;        // for the "checking N/M" progress line

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
  wxButton *     m_exportSel;
  wxButton *     m_exportVisible;

  static int s_sessionVolume; // 0..100, remembered across dialog opens for the session
};

#endif // VOICELINESDIALOG_H
