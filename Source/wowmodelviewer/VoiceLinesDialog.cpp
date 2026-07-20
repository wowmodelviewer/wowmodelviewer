/*
 * VoiceLinesDialog.cpp -- see VoiceLinesDialog.h. Creature / Boss VO Browser (V3).
 */
#include "VoiceLinesDialog.h"

#include <wx/filedlg.h>

#include <algorithm>
#include <cstdio>
#include <set>

#include "AudioPlayer.h"
#include "Game.h"         // GAMEDIRECTORY
#include "GameFile.h"
#include "globalvars.h"

int VoiceLinesDialog::s_sessionVolume = 100;

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
  ID_VL_EXPORT
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
  EVT_BUTTON(ID_VL_EXPORT, VoiceLinesDialog::OnExport)
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
  return q2w(source);
}

wxString VoiceLinesDialog::StatusLabel(int st)
{
  switch (st)
  {
    case 0:  return wxT("Playable");
    case 1:  return wxT("Encrypted / missing key");
    case 2:  return wxT("Missing from build");
    default: return wxT("Unknown");
  }
}

int VoiceLinesDialog::entryStatus(int fileDataId)
{
  std::map<int, int>::iterator it = m_statusCache.find(fileDataId);
  if (it != m_statusCache.end())
    return it->second;
  const int st = GAMEDIRECTORY.fileKeyStatus(fileDataId); // 0 playable / 1 encrypted / 2 missing / -1 unknown
  m_statusCache[fileDataId] = st;
  return st;
}

VoiceLinesDialog::VoiceLinesDialog(wxWindow * parent, const wxString & creatureName,
                                   const std::vector<VoiceLineEntry> & soundLines,
                                   const std::vector<VoiceLineEntry> & voiceFolderLines,
                                   const std::vector<VoiceLineEntry> & audioFolderLines,
                                   const std::vector<VoiceLineEntry> & encounterLines)
  : wxDialog(parent, wxID_ANY, wxT("Voice Lines - ") + creatureName, wxDefaultPosition,
             wxSize(880, 520), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_creatureName(creatureName), m_soundLines(soundLines), m_voiceFolderLines(voiceFolderLines),
    m_audioFolderLines(audioFolderLines), m_encounterLines(encounterLines), m_player(new AudioPlayer())
{
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);

  // Source + Sort row.
  wxBoxSizer * srcRow = new wxBoxSizer(wxHORIZONTAL);
  srcRow->Add(new wxStaticText(this, wxID_ANY, wxT("Source:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_source = new wxChoice(this, ID_VL_SOURCE);
  m_source->Append(wxT("Creature Sounds"));       // index 0 -> m_soundLines
  m_source->Append(wxT("Creature Voice Lines"));  // index 1 -> m_voiceFolderLines
  m_source->Append(wxT("Creature Audio Folder")); // index 2 -> m_audioFolderLines
  m_source->Append(wxT("Encounter Dialogue"));    // index 3 -> m_encounterLines
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

  // Multi-column list.
  m_list = new wxListCtrl(this, ID_VL_LIST, wxDefaultPosition, wxDefaultSize,
                          wxLC_REPORT | wxLC_SINGLE_SEL);
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

  // Export + close row.
  wxBoxSizer * btnRow = new wxBoxSizer(wxHORIZONTAL);
  m_export = new wxButton(this, ID_VL_EXPORT, wxT("Export Original"));
  btnRow->Add(m_export, 0, wxRIGHT, 8);
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
  RefillList();
}

const std::vector<VoiceLineEntry> & VoiceLinesDialog::activeLines() const
{
  switch (m_source->GetSelection())
  {
    case 1:  return m_voiceFolderLines;
    case 2:  return m_audioFolderLines;
    case 3:  return m_encounterLines;
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
      wxString hay = q2w(e.label) + wxT(" ") + fileText + wxT(" ")
                   + wxString::Format(wxT("%d %d "), e.fileDataId, e.soundKitId)
                   + SourceLabel(e.source) + wxT(" ") + q2w(e.category) + wxT(" ")
                   + StatusLabel(entryStatus(e.fileDataId));
      if (hay.Lower().Find(needle) == wxNOT_FOUND)
        continue;
    }
    idx.push_back((int)i);
  }

  // 2) sort the surviving indices per the sort choice. Statuses are probed up-front so the
  // comparator only reads the cache (never mutates it mid-sort).
  const int sortMode = m_sort->GetSelection();
  if (sortMode == VLSORT_STATUS)
    for (size_t k = 0; k < idx.size(); ++k)
      entryStatus(lines[idx[k]].fileDataId);
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
        const int sa = m_statusCache.count(ea.fileDataId) ? m_statusCache[ea.fileDataId] : -1;
        const int sb = m_statusCache.count(eb.fileDataId) ? m_statusCache[eb.fileDataId] : -1;
        if (sa != sb) return sa < sb;              // playable(0) < encrypted(1) < missing(2)
        return ea.fileDataId < eb.fileDataId;
      }
      default: // VLSORT_DEFAULT: keep the resolver's natural order
        return a < b;
    }
  });

  // 3) fill the report list.
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
    m_list->SetItem(row, COL_STATUS, StatusLabel(entryStatus(e.fileDataId))); // playable/encrypted/missing
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
                     : wxT("No creature sounds found for this model."));
  }
  else
  {
    SetStatus(wxString::Format(wxT("Showing %d of %d file(s). Select one and press Play."),
                               (int)m_filtered.size(), (int)lines.size()));
  }
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

void VoiceLinesDialog::OnExport(wxCommandEvent &)
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

  // Preserve the real extension if the file has a listfile name; otherwise infer from the header.
  wxString ext;
  wxString fullname = q2w(f->fullname());
  int dot = fullname.Find(wxT('.'), true);
  if (dot != wxNOT_FOUND && dot + 1 < (int)fullname.length())
    ext = fullname.Mid(dot + 1).Lower();
  if (ext.IsEmpty() || ext.Find(wxT('/')) != wxNOT_FOUND || ext.length() > 4)
    ext = inferAudioExtension(f->getBuffer(), f->getSize());
  if (ext.IsEmpty())
    ext = wxT("bin");

  // Default filename: for a named file keep its real basename (vo_73_aggramar_01_m); otherwise
  // fall back to CreatureName_Category_SoundKitID_FileDataID.
  wxString base;
  wxString wpath = q2w(e->filePath);
  if (!wpath.IsEmpty())
  {
    int sl = wpath.Find(wxT('/'), true);
    wxString bn = (sl != wxNOT_FOUND) ? wpath.Mid(sl + 1) : wpath;
    int d = bn.Find(wxT('.'), true);
    if (d != wxNOT_FOUND)
      bn = bn.Mid(0, d);
    base = sanitizeFileName(bn);
  }
  else
  {
    wxString cname = m_creatureName.IsEmpty() ? wxString(wxT("Creature")) : m_creatureName;
    wxString wcat = q2w(e->category);
    base = sanitizeFileName(wxString::Format(wxT("%s_%s_%d_%d"),
        cname, wcat, e->soundKitId, e->fileDataId));
  }

  wxString path = wxFileSelector(wxT("Export original audio"), wxGetCwd(), base + wxT(".") + ext, ext,
      ext.Upper() + wxT(" files (*.") + ext + wxT(")|*.") + ext + wxT("|All files (*.*)|*.*"),
      wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);

  if (path.IsEmpty())
  {
    f->close();
    return; // user cancelled
  }

  FILE * out = fopen(path.mb_str(), "wb");
  if (out)
  {
    fwrite(f->getBuffer(), 1, f->getSize(), out);
    fclose(out);
    SetStatus(wxT("Exported: ") + path);
  }
  else
  {
    SetStatus(wxT("Export failed (could not write file)."));
  }
  f->close();
}

void VoiceLinesDialog::OnCloseButton(wxCommandEvent &)
{
  if (m_player)
    m_player->stop();
  EndModal(wxID_CLOSE);
}

void VoiceLinesDialog::OnClose(wxCloseEvent &)
{
  if (m_player)
    m_player->stop();
  EndModal(wxID_CLOSE);
}
