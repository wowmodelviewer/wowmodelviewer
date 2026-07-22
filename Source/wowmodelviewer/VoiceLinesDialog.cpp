/*
 * VoiceLinesDialog.cpp -- see VoiceLinesDialog.h. Creature / Boss VO Browser (V3).
 */
#include "VoiceLinesDialog.h"

#include <wx/filedlg.h>
#include <wx/dirdlg.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#include "AudioPlayer.h"
#include "Game.h"         // GAMEDIRECTORY, core::Game::instance().configFolder()
#include "GameFile.h"
#include "globalvars.h"

int VoiceLinesDialog::s_sessionVolume = 100;

// Availability status is probed off the open path, so a row starts life "not yet checked".
static const int ST_PENDING = -2;
// How many CASC key-status probes to run per timer tick. Small enough that the UI stays responsive
// between ticks (each probe touches CASC); large enough that a folder of ~50 files fills in quickly.
static const int STATUS_BATCH = 4;

// The key-status cache is kept ALIVE between dialog opens, per loaded client, so re-opening the
// browser for the same model (or any model in the same build) shows availability instantly instead
// of re-probing CASC. Keyed by the client's data/config folder (distinguishes builds/products).
static std::map<std::string, std::map<int, int> > s_statusByBuild;

// VoiceLineEntry fields are Qt QStrings (resolved in the wow lib); convert to wxString at the UI boundary.
static wxString q2w(const QString & s)
{
  return wxString::FromUTF8(s.toUtf8().constData());
}

enum
{
  ID_VL_SOURCE = wxID_HIGHEST + 501,
  ID_VL_CATEGORY,
  ID_VL_SEARCH,
  ID_VL_SORT,
  ID_VL_LIST,
  ID_VL_PLAY,
  ID_VL_STOP,
  ID_VL_VOLUME,
  ID_VL_EXPORT_SEL,
  ID_VL_EXPORT_VIS,
  ID_VL_STATUSTIMER
};

// Column indices for the report-mode list.
enum { COL_LABEL = 0, COL_CATEGORY, COL_FILE, COL_FDID, COL_KIT, COL_SOURCE, COL_STATUS, COL_COUNT };

// Sort-choice indices. (VLSORT_ prefix avoids clashing with the windows.h SORT_DEFAULT macro.)
enum { VLSORT_DEFAULT = 0, VLSORT_SOURCE, VLSORT_CATEGORY, VLSORT_FILENAME,
       VLSORT_FDID, VLSORT_KIT, VLSORT_STATUS };

BEGIN_EVENT_TABLE(VoiceLinesDialog, wxDialog)
  EVT_CHOICE(ID_VL_SOURCE, VoiceLinesDialog::OnSource)
  EVT_CHOICE(ID_VL_CATEGORY, VoiceLinesDialog::OnCategory)
  EVT_TEXT(ID_VL_SEARCH, VoiceLinesDialog::OnSearch)
  EVT_CHOICE(ID_VL_SORT, VoiceLinesDialog::OnSort)
  EVT_LIST_ITEM_ACTIVATED(ID_VL_LIST, VoiceLinesDialog::OnLineActivate)
  EVT_BUTTON(ID_VL_PLAY, VoiceLinesDialog::OnPlay)
  EVT_BUTTON(ID_VL_STOP, VoiceLinesDialog::OnStop)
  EVT_SLIDER(ID_VL_VOLUME, VoiceLinesDialog::OnVolume)
  EVT_BUTTON(ID_VL_EXPORT_SEL, VoiceLinesDialog::OnExportSelected)
  EVT_BUTTON(ID_VL_EXPORT_VIS, VoiceLinesDialog::OnExportVisible)
  EVT_TIMER(ID_VL_STATUSTIMER, VoiceLinesDialog::OnStatusTimer)
  EVT_BUTTON(wxID_CLOSE, VoiceLinesDialog::OnCloseButton)
  EVT_CLOSE(VoiceLinesDialog::OnClose)
END_EVENT_TABLE()

// Infer a file extension for a raw audio buffer whose name/extension is unknown, from its header magic.
static wxString inferAudioExtension(const unsigned char * buf, size_t len)
{
  if (buf && len >= 4 && buf[0] == 'O' && buf[1] == 'g' && buf[2] == 'g' && buf[3] == 'S')
    return wxT("ogg");
  if (buf && len >= 12 && buf[0] == 'R' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == 'F' &&
      buf[8] == 'W' && buf[9] == 'A' && buf[10] == 'V' && buf[11] == 'E')
    return wxT("wav");
  if (buf && len >= 3 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3')
    return wxT("mp3");
  if (buf && len >= 2 && buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0) // MP3 frame sync
    return wxT("mp3");
  return wxEmptyString;
}

static wxString sanitizeFileName(const wxString & in)
{
  wxString out = in;
  const wxString bad = wxT("\\/:*?\"<>|");
  for (size_t i = 0; i < out.length(); ++i)
    if (bad.Find(out[i]) != wxNOT_FOUND)
      out[i] = wxT('_');
  return out;
}

// A tidy filename slug: strip path-illegal characters and fold whitespace to '-' so the underscore
// separators in the export filename pattern stay unambiguous.
static wxString slug(const wxString & in)
{
  wxString s = sanitizeFileName(in);
  s.Replace(wxT(" "), wxT("-"));
  s.Replace(wxT("\t"), wxT("-"));
  return s;
}

// Minimal RFC-4180 CSV field escaping for the export manifest.
static wxString csvField(const wxString & in)
{
  if (in.Find(wxT(',')) == wxNOT_FOUND && in.Find(wxT('"')) == wxNOT_FOUND &&
      in.Find(wxT('\n')) == wxNOT_FOUND && in.Find(wxT('\r')) == wxNOT_FOUND)
    return in;
  wxString s = in;
  s.Replace(wxT("\""), wxT("\"\""));
  return wxT("\"") + s + wxT("\"");
}

wxString VoiceLinesDialog::BaseName(const QString & path)
{
  if (path.isEmpty())
    return wxEmptyString;
  QString p = path;
  p.replace('\\', '/');
  int sl = p.lastIndexOf('/');
  return q2w(sl >= 0 ? p.mid(sl + 1) : p);
}

wxString VoiceLinesDialog::SourceLabel(const QString & source)
{
  if (source == "CreatureSound")       return wxT("Creature Sound");
  if (source == "CreatureVoiceFolder") return wxT("Creature Voice Folder");
  if (source == "CreatureAudioFolder") return wxT("Creature Audio Folder");
  if (source == "EncounterDialogue")   return wxT("Encounter Dialogue");
  if (source == "Candidate")           return wxT("Candidate (unverified)");
  return q2w(source);
}

// Short lowercase slug used to prefix export filenames (keeps them terse and machine-friendly).
wxString VoiceLinesDialog::SourceToken(const QString & source)
{
  if (source == "CreatureSound")       return wxT("creaturesound");
  if (source == "CreatureVoiceFolder") return wxT("voicefolder");
  if (source == "CreatureAudioFolder") return wxT("audiofolder");
  if (source == "EncounterDialogue")   return wxT("encounter");
  if (source == "Candidate")           return wxT("candidate");
  return slug(q2w(source)).Lower();
}

wxString VoiceLinesDialog::StatusLabel(int st)
{
  switch (st)
  {
    case 0:          return wxT("Playable");
    case 1:          return wxT("Encrypted / missing key");
    case 2:          return wxT("Missing from build");
    case ST_PENDING: return wxT("Checking...");
    default:         return wxT("Unknown");
  }
}

// BLOCKING availability probe. Only the status timer calls this; it touches CASC, so it must never
// run on the dialog-open path. The result is cached (per build) for the life of the process.
int VoiceLinesDialog::probeStatus(int fileDataId)
{
  std::map<int, int>::iterator it = m_statusCache->find(fileDataId);
  if (it != m_statusCache->end())
    return it->second;
  const int st = GAMEDIRECTORY.fileKeyStatus(fileDataId); // 0 playable / 1 encrypted / 2 missing / -1 unknown
  (*m_statusCache)[fileDataId] = st;
  return st;
}

// NON-blocking lookup: cached value, or ST_PENDING if this id has not been probed yet.
int VoiceLinesDialog::cachedStatus(int fileDataId) const
{
  std::map<int, int>::const_iterator it = m_statusCache->find(fileDataId);
  return it != m_statusCache->end() ? it->second : ST_PENDING;
}

VoiceLinesDialog::VoiceLinesDialog(wxWindow * parent, const wxString & creatureName,
                                   const std::vector<VoiceLineEntry> & soundLines,
                                   const std::vector<VoiceLineEntry> & voiceFolderLines,
                                   const std::vector<VoiceLineEntry> & audioFolderLines,
                                   const std::vector<VoiceLineEntry> & encounterLines,
                                   const std::vector<VoiceLineEntry> & candidateLines)
  : wxDialog(parent, wxID_ANY, wxT("Voice Lines - ") + creatureName, wxDefaultPosition,
             wxSize(900, 540), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_creatureName(creatureName), m_soundLines(soundLines), m_voiceFolderLines(voiceFolderLines),
    m_audioFolderLines(audioFolderLines), m_encounterLines(encounterLines),
    m_candidateLines(candidateLines), m_statusCache(NULL), m_statusTimer(NULL), m_statusTotal(0),
    m_player(new AudioPlayer())
{
  // Point at this build's persistent status cache (created on first use).
  const std::string buildKey(core::Game::instance().configFolder().toUtf8().constData());
  m_statusCache = &s_statusByBuild[buildKey];

  m_statusTimer = new wxTimer(this, ID_VL_STATUSTIMER);

  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);

  // Source + Sort row.
  wxBoxSizer * srcRow = new wxBoxSizer(wxHORIZONTAL);
  srcRow->Add(new wxStaticText(this, wxID_ANY, wxT("Source:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_source = new wxChoice(this, ID_VL_SOURCE);
  m_source->Append(wxT("Creature Sounds"));       // index 0 -> m_soundLines
  m_source->Append(wxT("Creature Voice Lines"));  // index 1 -> m_voiceFolderLines
  m_source->Append(wxT("Creature Audio Folder")); // index 2 -> m_audioFolderLines
  m_source->Append(wxT("Encounter Dialogue"));    // index 3 -> m_encounterLines
  // index 4 -- debug audition list, only offered when WMV_VL_CANDIDATES supplied one. These are
  // unattributed batch-mates, never a confirmed voice for the selected creature.
  if (!m_candidateLines.empty())
    m_source->Append(wxT("Candidates (unverified)"));
  m_source->SetSelection(!m_encounterLines.empty() ? 3 : (!m_voiceFolderLines.empty() ? 1 : (!m_soundLines.empty() ? 0 : (!m_audioFolderLines.empty() ? 2 : 0))));
  srcRow->Add(m_source, 1, wxEXPAND | wxRIGHT, 12);
  srcRow->Add(new wxStaticText(this, wxID_ANY, wxT("Sort:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_sort = new wxChoice(this, ID_VL_SORT);
  m_sort->Append(wxT("Default"));
  m_sort->Append(wxT("Source"));
  m_sort->Append(wxT("Category"));
  m_sort->Append(wxT("Filename"));
  m_sort->Append(wxT("FileDataID"));
  m_sort->Append(wxT("SoundKitID"));
  m_sort->Append(wxT("Status"));
  m_sort->SetSelection(VLSORT_DEFAULT);
  srcRow->Add(m_sort, 0);
  top->Add(srcRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

  // Category + Search row.
  wxBoxSizer * filtRow = new wxBoxSizer(wxHORIZONTAL);
  filtRow->Add(new wxStaticText(this, wxID_ANY, wxT("Category:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_category = new wxChoice(this, ID_VL_CATEGORY);
  filtRow->Add(m_category, 0, wxRIGHT, 12);
  filtRow->Add(new wxStaticText(this, wxID_ANY, wxT("Search:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_search = new wxTextCtrl(this, ID_VL_SEARCH);
  m_search->SetToolTip(wxT("Filter by label, filename, FileDataID, SoundKitID, source or category"));
  filtRow->Add(m_search, 1, wxEXPAND);
  top->Add(filtRow, 0, wxEXPAND | wxALL, 8);

  // Multi-column list. Multi-select is deliberate: it drives "Export Selected...".
  m_list = new wxListCtrl(this, ID_VL_LIST, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
  m_list->InsertColumn(COL_LABEL,    wxT("Label"),      wxLIST_FORMAT_LEFT,  110);
  m_list->InsertColumn(COL_CATEGORY, wxT("Category"),   wxLIST_FORMAT_LEFT,  85);
  m_list->InsertColumn(COL_FILE,     wxT("Filename"),   wxLIST_FORMAT_LEFT,  205);
  m_list->InsertColumn(COL_FDID,     wxT("FileDataID"), wxLIST_FORMAT_RIGHT, 80);
  m_list->InsertColumn(COL_KIT,      wxT("SoundKitID"), wxLIST_FORMAT_RIGHT, 80);
  m_list->InsertColumn(COL_SOURCE,   wxT("Source"),     wxLIST_FORMAT_LEFT,  115);
  m_list->InsertColumn(COL_STATUS,   wxT("Status"),     wxLIST_FORMAT_LEFT,  150);
  top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  // Playback + volume row.
  wxBoxSizer * ctrlRow = new wxBoxSizer(wxHORIZONTAL);
  m_play = new wxButton(this, ID_VL_PLAY, wxT("Play"));
  m_stop = new wxButton(this, ID_VL_STOP, wxT("Stop"));
  ctrlRow->Add(m_play, 0, wxRIGHT, 4);
  ctrlRow->Add(m_stop, 0, wxRIGHT, 12);
  m_volumeLabel = new wxStaticText(this, wxID_ANY, wxT("Volume: 100%"));
  ctrlRow->Add(m_volumeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_volume = new wxSlider(this, ID_VL_VOLUME, s_sessionVolume, 0, 100, wxDefaultPosition, wxSize(120, -1));
  ctrlRow->Add(m_volume, 1, wxALIGN_CENTER_VERTICAL);
  top->Add(ctrlRow, 0, wxEXPAND | wxALL, 8);

  // Export + close row. Audio is exported verbatim (original OGG bytes) -- there is no format
  // selector because no other encoder is bundled.
  wxBoxSizer * btnRow = new wxBoxSizer(wxHORIZONTAL);
  m_exportSel = new wxButton(this, ID_VL_EXPORT_SEL, wxT("Export Selected..."));
  m_exportSel->SetToolTip(wxT("Export the selected row(s) to a folder, with a manifest.csv"));
  btnRow->Add(m_exportSel, 0, wxRIGHT, 8);
  m_exportVisible = new wxButton(this, ID_VL_EXPORT_VIS, wxT("Export All Visible..."));
  m_exportVisible->SetToolTip(wxT("Export every row currently shown (after the filter), with a manifest.csv"));
  btnRow->Add(m_exportVisible, 0, wxRIGHT, 8);
  btnRow->AddStretchSpacer(1);
  btnRow->Add(new wxButton(this, wxID_CLOSE, wxT("Close")), 0);
  top->Add(btnRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

  // Status line.
  m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_status, 0, wxEXPAND | wxALL, 8);

  SetSizer(top);

  m_volumeLabel->SetLabel(wxString::Format(wxT("Volume: %d%%"), s_sessionVolume));
  m_player->setVolume(s_sessionVolume / 100.0f);

  RebuildCategories();
  RefillList(); // populates instantly; availability fills in via the status timer
}

const std::vector<VoiceLineEntry> & VoiceLinesDialog::activeLines() const
{
  switch (m_source->GetSelection())
  {
    case 1:  return m_voiceFolderLines;
    case 2:  return m_audioFolderLines;
    case 3:  return m_encounterLines;
    case 4:  return m_candidateLines;
    default: return m_soundLines;
  }
}

void VoiceLinesDialog::RebuildCategories()
{
  m_category->Clear();
  m_category->Append(wxT("All"));
  std::set<wxString> seen;
  const std::vector<VoiceLineEntry> & lines = activeLines();
  for (size_t i = 0; i < lines.size(); ++i)
  {
    const wxString c = q2w(lines[i].category);
    if (seen.insert(c).second)
      m_category->Append(c);
  }
  m_category->SetSelection(0);
}

void VoiceLinesDialog::OnSource(wxCommandEvent &)
{
  m_player->stop();
  RebuildCategories();
  RefillList();
}

void VoiceLinesDialog::OnCategory(wxCommandEvent &) { RefillList(); }
void VoiceLinesDialog::OnSearch(wxCommandEvent &)   { RefillList(); }
void VoiceLinesDialog::OnSort(wxCommandEvent &)     { RefillList(); }

VoiceLinesDialog::~VoiceLinesDialog()
{
  if (m_statusTimer)
  {
    m_statusTimer->Stop();
    delete m_statusTimer;
    m_statusTimer = NULL;
  }
  if (m_player)
  {
    m_player->stop();
    delete m_player;
    m_player = NULL;
  }
}

void VoiceLinesDialog::SetStatus(const wxString & msg)
{
  if (m_status)
    m_status->SetLabel(msg);
}

void VoiceLinesDialog::RefillList()
{
  m_list->DeleteAllItems();
  m_filtered.clear();
  const std::vector<VoiceLineEntry> & lines = activeLines();

  const bool allCat = (m_category->GetSelection() <= 0);
  const wxString cat = allCat ? wxString() : m_category->GetStringSelection();
  const wxString needle = m_search->GetValue().Lower();

  // 1) collect candidate indices (category filter + free-text search).
  std::vector<int> idx;
  for (size_t i = 0; i < lines.size(); ++i)
  {
    const VoiceLineEntry & e = lines[i];
    if (!allCat && q2w(e.category) != cat)
      continue;
    if (!needle.IsEmpty())
    {
      const wxString fileText = e.filePath.isEmpty() ? wxString(wxT("(unnamed)")) : BaseName(e.filePath);
      // Status uses the cached value only -- searching must never trigger a blocking CASC probe.
      wxString hay = q2w(e.label) + wxT(" ") + fileText + wxT(" ")
                   + wxString::Format(wxT("%d %d "), e.fileDataId, e.soundKitId)
                   + SourceLabel(e.source) + wxT(" ") + q2w(e.category) + wxT(" ")
                   + StatusLabel(cachedStatus(e.fileDataId));
      if (hay.Lower().Find(needle) == wxNOT_FOUND)
        continue;
    }
    idx.push_back((int)i);
  }

  // 2) sort the surviving indices per the sort choice. The comparator only reads cached statuses
  // (never probes); rows not yet checked sort as ST_PENDING and settle once the timer finishes.
  const int sortMode = m_sort->GetSelection();
  std::sort(idx.begin(), idx.end(), [&](int a, int b) {
    const VoiceLineEntry & ea = lines[a];
    const VoiceLineEntry & eb = lines[b];
    switch (sortMode)
    {
      case VLSORT_SOURCE:
      {
        int c = SourceLabel(ea.source).CmpNoCase(SourceLabel(eb.source));
        if (c != 0) return c < 0;
        c = q2w(ea.category).CmpNoCase(q2w(eb.category));
        if (c != 0) return c < 0;
        return ea.variation < eb.variation;
      }
      case VLSORT_CATEGORY:
      {
        int c = q2w(ea.category).CmpNoCase(q2w(eb.category));
        if (c != 0) return c < 0;
        return ea.variation < eb.variation;
      }
      case VLSORT_FILENAME:
        return BaseName(ea.filePath).CmpNoCase(BaseName(eb.filePath)) < 0;
      case VLSORT_FDID:
        return ea.fileDataId < eb.fileDataId;
      case VLSORT_KIT:
        return ea.soundKitId < eb.soundKitId;
      case VLSORT_STATUS:
      {
        const int sa = cachedStatus(ea.fileDataId);
        const int sb = cachedStatus(eb.fileDataId);
        if (sa != sb) return sa < sb;              // playable(0) < encrypted(1) < missing(2)
        return ea.fileDataId < eb.fileDataId;
      }
      default: // VLSORT_DEFAULT: keep the resolver's natural order
        return a < b;
    }
  });

  // 3) fill the report list (no blocking probes -- Status shows the cached value or "Checking...").
  for (size_t r = 0; r < idx.size(); ++r)
  {
    const VoiceLineEntry & e = lines[idx[r]];
    long row = m_list->InsertItem((long)r, q2w(e.label));
    m_list->SetItem(row, COL_CATEGORY, q2w(e.category)); // Aggro/Alert/... , Voice Line, or Folder Audio
    // Three naming states: friendly path, generic/numeric path, or none -> "(unnamed)". Never hidden.
    m_list->SetItem(row, COL_FILE, e.filePath.isEmpty() ? wxString(wxT("(unnamed)")) : BaseName(e.filePath));
    m_list->SetItem(row, COL_FDID, wxString::Format(wxT("%d"), e.fileDataId));
    m_list->SetItem(row, COL_KIT, e.soundKitId ? wxString::Format(wxT("%d"), e.soundKitId) : wxString(wxT("-")));
    m_list->SetItem(row, COL_SOURCE, SourceLabel(e.source));
    m_list->SetItem(row, COL_STATUS, StatusLabel(cachedStatus(e.fileDataId)));
    m_filtered.push_back(idx[r]);
  }

  if (!m_filtered.empty())
  {
    m_list->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                         wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
  }

  if (lines.empty())
  {
    const int s = m_source->GetSelection();
    SetStatus(s == 1 ? wxT("No creature voice lines found for this model.")
            : s == 2 ? wxT("No audio files found in this creature's sound folder.")
            : s == 3 ? wxT("No encounter dialogue found (model not linked to a journal encounter).")
            : s == 4 ? wxT("Candidate list is empty.")
                     : wxT("No creature sounds found for this model."));
  }

  // Kick off (or refresh) the background availability probe for whatever is now visible.
  startStatusProbe();
}

// Queue every still-unchecked visible FileDataID and start the incremental timer. Export is disabled
// while a probe is outstanding (so we never export a file whose availability we haven't confirmed).
void VoiceLinesDialog::startStatusProbe()
{
  if (m_statusTimer)
    m_statusTimer->Stop();
  m_pending.clear();

  std::set<int> queued;
  const std::vector<VoiceLineEntry> & lines = activeLines();
  for (size_t r = 0; r < m_filtered.size(); ++r)
  {
    const int fdid = lines[m_filtered[r]].fileDataId;
    if (cachedStatus(fdid) == ST_PENDING && queued.insert(fdid).second)
      m_pending.push_back(fdid);
  }

  if (m_pending.empty())
  {
    m_exportSel->Enable(true);
    m_exportVisible->Enable(true);
    if (!m_filtered.empty())
      SetStatus(wxString::Format(wxT("Showing %d of %d file(s). Select one and press Play."),
                                 (int)m_filtered.size(), (int)activeLines().size()));
    return;
  }

  m_statusTotal = m_pending.size();
  m_exportSel->Enable(false);
  m_exportVisible->Enable(false);
  SetStatus(wxString::Format(wxT("Loading voice lines... checking availability (0/%d)"),
                             (int)m_statusTotal));
  m_statusTimer->Start(15); // continuous; drained in OnStatusTimer, stopped when m_pending empties
}

void VoiceLinesDialog::OnStatusTimer(wxTimerEvent &)
{
  for (int n = 0; n < STATUS_BATCH && !m_pending.empty(); ++n)
  {
    probeStatus(m_pending.front()); // blocking, but only STATUS_BATCH per tick -> UI stays responsive
    m_pending.erase(m_pending.begin());
  }

  refreshVisibleStatus();

  if (m_pending.empty())
  {
    m_statusTimer->Stop();
    m_exportSel->Enable(true);
    m_exportVisible->Enable(true);
    // Status ordering is only meaningful once every row is checked; re-sort now (cache is warm, so
    // this pass does no probing and starts no new timer).
    if (m_sort->GetSelection() == VLSORT_STATUS)
    {
      RefillList();
      return;
    }
    if (!m_filtered.empty())
      SetStatus(wxString::Format(wxT("Showing %d of %d file(s). Select one and press Play."),
                                 (int)m_filtered.size(), (int)activeLines().size()));
  }
  else
  {
    SetStatus(wxString::Format(wxT("Loading voice lines... checking availability (%d/%d)"),
                               (int)(m_statusTotal - m_pending.size()), (int)m_statusTotal));
  }
}

// Repaint just the Status column of the visible rows from the (now partly warm) cache.
void VoiceLinesDialog::refreshVisibleStatus()
{
  const std::vector<VoiceLineEntry> & lines = activeLines();
  for (size_t r = 0; r < m_filtered.size(); ++r)
    m_list->SetItem((long)r, COL_STATUS, StatusLabel(cachedStatus(lines[m_filtered[r]].fileDataId)));
}

const VoiceLineEntry * VoiceLinesDialog::SelectedEntry() const
{
  long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel == -1 || sel < 0 || (size_t)sel >= m_filtered.size())
    return NULL;
  const std::vector<VoiceLineEntry> & lines = activeLines();
  int i = m_filtered[sel];
  if (i < 0 || (size_t)i >= lines.size())
    return NULL;
  return &lines[i];
}

std::vector<int> VoiceLinesDialog::selectedLineIndices() const
{
  std::vector<int> out;
  long sel = -1;
  while ((sel = m_list->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
    if (sel >= 0 && (size_t)sel < m_filtered.size())
      out.push_back(m_filtered[sel]);
  return out;
}

std::vector<int> VoiceLinesDialog::visibleLineIndices() const
{
  return m_filtered;
}

void VoiceLinesDialog::OnLineActivate(wxListEvent &)
{
  wxCommandEvent dummy;
  OnPlay(dummy);
}

void VoiceLinesDialog::OnPlay(wxCommandEvent &)
{
  const VoiceLineEntry * e = SelectedEntry();
  if (!e)
  {
    SetStatus(wxT("Select a line first."));
    return;
  }

  GameFile * f = GAMEDIRECTORY.getFile((uint)e->fileDataId);
  if (!f)
  {
    SetStatus(wxString::Format(wxT("Audio file %d not found."), e->fileDataId));
    return;
  }
  f->open();
  if (f->isEof() || f->getSize() == 0)
  {
    SetStatus(wxString::Format(wxT("Audio file %d is empty/unreadable."), e->fileDataId));
    f->close();
    return;
  }

  const bool ok = m_player->playBytes(f->getBuffer(), f->getSize(), s_sessionVolume / 100.0f);
  f->close();
  if (ok)
    SetStatus(wxT("Playing: ") + q2w(e->label));
  else
    SetStatus(wxString::Format(wxT("Could not play %d: %s"), e->fileDataId,
                               wxString::FromUTF8(m_player->lastError()).c_str()));
}

void VoiceLinesDialog::OnStop(wxCommandEvent &)
{
  m_player->stop();
  SetStatus(wxT("Stopped."));
}

void VoiceLinesDialog::OnVolume(wxCommandEvent &)
{
  s_sessionVolume = m_volume->GetValue();
  m_volumeLabel->SetLabel(wxString::Format(wxT("Volume: %d%%"), s_sessionVolume));
  m_player->setVolume(s_sessionVolume / 100.0f);
}

void VoiceLinesDialog::OnExportSelected(wxCommandEvent &)
{
  std::vector<int> sel = selectedLineIndices();
  if (sel.empty())
  {
    SetStatus(wxT("Select one or more rows first."));
    return;
  }
  exportRows(sel);
}

void VoiceLinesDialog::OnExportVisible(wxCommandEvent &)
{
  std::vector<int> vis = visibleLineIndices();
  if (vis.empty())
  {
    SetStatus(wxT("Nothing to export -- the list is empty."));
    return;
  }
  exportRows(vis);
}

// Write each requested line (raw, original OGG bytes) into a chosen folder, tally the results, and
// drop a manifest.csv alongside. Unavailable files (missing / encrypted) are skipped, not faked.
void VoiceLinesDialog::exportRows(const std::vector<int> & lineIdx)
{
  if (lineIdx.empty())
    return;

  const wxString folder = wxDirSelector(wxT("Choose a folder to export audio into"), wxGetCwd(),
                                        0, wxDefaultPosition, this);
  if (folder.IsEmpty())
    return; // cancelled

  const std::vector<VoiceLineEntry> & lines = activeLines();

  wxString manifestPath = folder + wxFILE_SEP_PATH + wxT("manifest.csv");
  FILE * manifest = fopen(manifestPath.mb_str(), "w");
  if (manifest)
    fputs("index,label,source,category,FileDataID,SoundKitID,original_path,status,exported_filename\n",
          manifest);

  int nExported = 0, nMissing = 0, nEncrypted = 0, nFailed = 0;
  std::set<wxString> usedNames; // avoid clobbering when two entries would produce the same filename

  for (size_t k = 0; k < lineIdx.size(); ++k)
  {
    const int li = lineIdx[k];
    if (li < 0 || (size_t)li >= lines.size())
      continue;
    const VoiceLineEntry & e = lines[li];

    const wxString label   = q2w(e.label);
    const wxString srcTok  = SourceToken(e.source);
    const wxString catSlug = slug(q2w(e.category));
    const wxString origPath = e.filePath.isEmpty() ? wxString(wxT("(unnamed)")) : q2w(e.filePath);

    wxString statusWord, outName;

    // Availability first: exports are only enabled once the probe finished, so this reads the cache.
    const int st = probeStatus(e.fileDataId);
    if (st == 1)
    {
      statusWord = wxT("skipped-encrypted");
      ++nEncrypted;
    }
    else if (st == 2)
    {
      statusWord = wxT("skipped-missing");
      ++nMissing;
    }
    else
    {
      GameFile * f = GAMEDIRECTORY.getFile((uint)e.fileDataId);
      if (!f)
      {
        statusWord = wxT("skipped-missing");
        ++nMissing;
      }
      else
      {
        f->open();
        if (f->isEof() || f->getSize() == 0)
        {
          statusWord = wxT("failed");
          ++nFailed;
          f->close();
        }
        else
        {
          wxString ext = inferAudioExtension(f->getBuffer(), f->getSize());
          if (ext.IsEmpty())
            ext = wxT("ogg"); // creature VO is Ogg Vorbis; a sane default when the magic is unusual

          // <source>_<category>_fdid_<FileDataID>_sk_<SoundKitID>.<ext>
          wxString base = wxString::Format(wxT("%s_%s_fdid_%d_sk_%d"),
              srcTok, catSlug, e.fileDataId, e.soundKitId);
          wxString name = base + wxT(".") + ext;
          int dup = 2;
          while (usedNames.count(name.Lower()))
            name = wxString::Format(wxT("%s_%d.%s"), base, dup++, ext);
          usedNames.insert(name.Lower());

          wxString outPath = folder + wxFILE_SEP_PATH + name;
          FILE * out = fopen(outPath.mb_str(), "wb");
          if (out)
          {
            fwrite(f->getBuffer(), 1, f->getSize(), out);
            fclose(out);
            statusWord = wxT("exported");
            outName = name;
            ++nExported;
          }
          else
          {
            statusWord = wxT("failed");
            ++nFailed;
          }
          f->close();
        }
      }
    }

    if (manifest)
      fprintf(manifest, "%d,%s,%s,%s,%d,%d,%s,%s,%s\n",
              (int)(k + 1),
              (const char *)csvField(label).mb_str(),
              (const char *)csvField(SourceLabel(e.source)).mb_str(),
              (const char *)csvField(q2w(e.category)).mb_str(),
              e.fileDataId, e.soundKitId,
              (const char *)csvField(origPath).mb_str(),
              (const char *)statusWord.mb_str(),
              (const char *)csvField(outName).mb_str());
  }

  if (manifest)
    fclose(manifest);

  wxString summary = wxString::Format(
      wxT("Exported %d file(s) to:\n%s\n\n")
      wxT("Skipped (missing from build): %d\n")
      wxT("Skipped (encrypted / no key): %d\n")
      wxT("Failed to write: %d\n\n")
      wxT("Manifest: manifest.csv"),
      nExported, folder, nMissing, nEncrypted, nFailed);
  wxMessageBox(summary, wxT("Voice Lines export"), wxOK | wxICON_INFORMATION, this);

  SetStatus(wxString::Format(
      wxT("Export complete: %d exported, %d missing, %d encrypted, %d failed. (manifest.csv written)"),
      nExported, nMissing, nEncrypted, nFailed));
}

void VoiceLinesDialog::OnCloseButton(wxCommandEvent &)
{
  if (m_statusTimer)
    m_statusTimer->Stop();
  if (m_player)
    m_player->stop();
  EndModal(wxID_CLOSE);
}

void VoiceLinesDialog::OnClose(wxCloseEvent &)
{
  if (m_statusTimer)
    m_statusTimer->Stop();
  if (m_player)
    m_player->stop();
  EndModal(wxID_CLOSE);
}
