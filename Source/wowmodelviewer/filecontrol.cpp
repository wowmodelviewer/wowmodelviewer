#include "logger/Logger.h"

#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/srchctrl.h>

#include <algorithm>
#include <functional>

#include <QDirIterator>
#include <QElapsedTimer>
#include <QImage>
#include <QRegularExpression>

#include "CASCFile.h"
#include "Game.h"
#include "globalvars.h"
#include "logger/Logger.h"
#include "modelcontrol.h"
#include "modelviewer.h"
#include "RaceInfos.h"
#include "TextureBrowse.h"
#include "TextureView.h"
#include "UiControls.h"
#include "UiStyle.h"

#include <commctrl.h>
#include "UnityAssetAccess.h"

IMPLEMENT_CLASS(FileControl, wxWindow)

BEGIN_EVENT_TABLE(FileControl, wxWindow)
  // model tree
  EVT_TREE_SEL_CHANGED(ID_FILELIST, FileControl::OnTreeSelect)
  EVT_TREE_ITEM_EXPANDING(ID_FILELIST, FileControl::OnTreeItemExpanding)
  EVT_SEARCH(ID_FILELIST_CONTENT, FileControl::OnButton)
  EVT_SEARCH_CANCEL(ID_FILELIST_CONTENT, FileControl::OnButton)
  EVT_TEXT(ID_FILELIST_CONTENT, FileControl::OnSearchText)
  EVT_TIMER(ID_FILELIST_SEARCHTIMER, FileControl::OnSearchTimer)
  EVT_TREE_ITEM_MENU(ID_FILELIST, FileControl::OnTreeMenu)
  EVT_TREE_ITEM_ACTIVATED(ID_FILELIST, FileControl::OnTreeActivated)
END_EVENT_TABLE()

// WHAT BROWSE LISTS is what the viewer mode shows (ModelViewer::SetViewerMode -> FollowViewerMode),
// with no choice of its own: in Models mode the models (*.m2), in Textures mode the client's textures
// (TextureBrowse), in Buildings mode the world model roots (*.wmo; see isWmoGroupFile). Other kinds of file
// are not listed: the viewer has nothing to show them with.

// A Buildings search lists at most this many buildings (the rest are counted): every building of a client
// matches a short enough term, and its rows are all made at once.
static const size_t BuildingsShown = 1000;

bool FileControl::isWmoGroupFile(const QString & path, const std::map<QString, GameFile *> & index)
{
  const int slash = (std::max)(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
  QString stem = path.mid(slash + 1);
  if (!stem.endsWith(QLatin1String(".wmo"), Qt::CaseInsensitive))
    return false;
  stem.chop(4);
  auto digit = [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); };
  // "..._lodN" (a level of detail of a group: "<root>_NNN_lodN"), then "..._NNN".
  int n = stem.size();
  if (n >= 5 && stem[n - 5] == QLatin1Char('_') && stem.mid(n - 4, 3).compare(QLatin1String("lod"), Qt::CaseInsensitive) == 0 &&
      digit(stem[n - 1]))
    stem.chop(5);
  n = stem.size();
  if (n < 5 || stem[n - 4] != QLatin1Char('_') || !digit(stem[n - 3]) || !digit(stem[n - 2]) || !digit(stem[n - 1]))
    return false;
  // A group only when its root is there beside it.
  const QString root = path.left(slash + 1) + stem.left(n - 4) + path.right(4);
  return index.count(root) != 0;
}

void beautifyFileName(QString & file)
{
  file = file.toLower().replace('/','\\');
  QString firstLetter = file[0];
  firstLetter = firstLetter.toUpper();
  file[0] = firstLetter[0];
  int ret = file.indexOf('\\');
  if (ret>-1)
  {
    firstLetter = file[ret+1];
    firstLetter = firstLetter.toUpper();
    file[ret+1] = firstLetter[0];
  }
}

FileControl::FileControl(wxWindow* parent, wxWindowID id)
{
  modelviewer = NULL;
  fileTree = NULL;
  m_searchTimer.SetOwner(this, ID_FILELIST_SEARCHTIMER);

  if (Create(parent, id, wxDefaultPosition, wxSize(260,700), 0, wxT("ModelControlFrame")) == false) {
    LOG_ERROR << "Failed to create a window for our FileControl!";
    return;
  }

  try {
    const int sp = FromDIP(UiStyle::S);

    UiStyle::applyPanel(this);

    // The search box, the tree, and one line under the tree: the minimum length while typing, how many
    // files a search found, or why there is nothing to list. The search is the viewer mode's (its hint
    // says which: UpdateSearchHint).
    UiSearchFrame * searchFrame = new UiSearchFrame(this, ID_FILELIST_CONTENT, wxEmptyString, wxTE_PROCESS_ENTER);
    txtContent = searchFrame->search();
    txtContent->ShowSearchButton(true);
    txtContent->ShowCancelButton(true);
    searchStatus = UiStyle::secondaryLabel(this, wxEmptyString);

    // The tree in the Explorer style (chevrons, hover, the soft selection), on the pane's own colour with
    // no border of its own (the pane has one), rows a little taller than the native minimum.
    fileTree = new wxTreeCtrl(this, ID_FILELIST, wxDefaultPosition, wxDefaultSize,
                              wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_FULL_ROW_HIGHLIGHT |
                              wxTR_NO_LINES | wxTR_TWIST_BUTTONS | wxBORDER_NONE);
    fileTree->EnableSystemTheme();
    fileTree->SetDoubleBuffered(true);
    fileTree->SetBackgroundColour(UiStyle::palette().panelBackground);
    fileTree->SetForegroundColour(UiStyle::palette().text);
    {
      const HWND tree = (HWND)fileTree->GetHWND();
      const int row = FromDIP(UiStyle::TreeRowHeight) & ~1;   // an even height: Windows rounds odd ones down
      if (TreeView_GetItemHeight(tree) < row)
        TreeView_SetItemHeight(tree, row);
    }
    m_textures = new TextureBrowse(fileTree);
    m_texturesLoadWatch.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { TexturesClientLoaded(); });
    // An arrow key held down in the texture tree: the rows it passes wait to be decoded until it is let
    // go (TextureView::setKeysRepeating). Focus leaving the tree lets go too: the key-up goes elsewhere.
    fileTree->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent & e) { KeysRepeating(e.IsAutoRepeat()); e.Skip(); });
    fileTree->Bind(wxEVT_KEY_UP, [this](wxKeyEvent & e) { KeysRepeating(false); e.Skip(); });
    fileTree->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent & e) { KeysRepeating(false); e.Skip(); });

    wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
    top->Add(searchFrame, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
    top->Add(fileTree, 1, wxEXPAND | wxTOP, sp);
    top->Add(searchStatus, 0, wxEXPAND | wxALL, sp);
    SetSizer(top);

    UpdateSearchHint();
    SetSearchStatus(_("Load a World of Warcraft client to browse its files."));
  } catch(...) {};
}

FileControl::~FileControl()
{
  m_texturesLoadWatch.Stop();
  delete m_textures;
  m_textures = NULL;
  if (fileTree) {
    fileTree->Destroy();
    fileTree = NULL;
  }
  // The search field together with its frame (UiSearchFrame), which refers to it.
  txtContent->GetParent()->Destroy();
  txtContent = NULL;
}

void FileControl::SetSearchStatus(const wxString & text)
{
  if (searchStatus->GetLabel() == text)
    return;
  searchStatus->SetLabel(text);
  searchStatus->Wrap(-1);   // wxWidgets 3.3 skips a Wrap at the width it last wrapped at, whatever the text
  searchStatus->Wrap(wxMax(FromDIP(100), GetClientSize().x - 2 * FromDIP(UiStyle::S)));
  searchStatus->Show(!text.IsEmpty());
  Layout();
}

void FileControl::UpdateSearchHint()
{
  if (m_mode == ViewerMode::Textures)
  {
    txtContent->SetDescriptiveText(_("Search textures"));
    txtContent->SetToolTip(_("Textures by name or path -- or FileDataID, on a client that has them -- as you type "
                             "from 3 characters; press Enter to search a shorter term, or to open the texture with "
                             "that FileDataID"));
  }
  else if (m_mode == ViewerMode::Buildings)
  {
    txtContent->SetDescriptiveText(_("Search buildings"));
    txtContent->SetToolTip(_("World Model Objects (WMO) by name or path -- or FileDataID, on a client that has them -- "
                             "as you type from 3 characters; press Enter to search a shorter term, or to open the "
                             "building with that FileDataID"));
  }
  else
  {
    txtContent->SetDescriptiveText(_("Search models"));
    txtContent->SetToolTip(_("Models by file name, as you type from 3 characters; press Enter to search a shorter term"));
  }
}

namespace
{
  // Emptying the tree hands it the keyboard: the Windows tree control takes the focus while its rows are
  // deleted, from the search box too. Typing on in the search box must not land in the tree, so whatever
  // had the keyboard before the tree was refilled has it again afterwards.
  class KeepFocusOffTree
  {
  public:
    explicit KeepFocusOffTree(wxWindow * tree) : m_tree((HWND)tree->GetHWND()), m_before(::GetFocus()) {}
    ~KeepFocusOffTree()
    {
      // Shown by wx's account, not IsWindowVisible: inside a frozen frame (a viewer-mode switch) every
      // window reads as invisible to Windows.
      wxWindow * before = m_before && ::IsWindow(m_before) ? wxFindWinFromHandle(m_before) : nullptr;
      const bool shown = before ? before->IsShownOnScreen() : (m_before && ::IsWindowVisible(m_before));
      if (m_before && m_before != m_tree && ::GetFocus() == m_tree && ::IsWindow(m_before) && shown &&
          ::IsWindowEnabled(m_before))
        ::SetFocus(m_before);
    }

  private:
    HWND m_tree;
    HWND m_before;
  };
}

void FileControl::Init(ModelViewer* mv)
{
  if (modelviewer == NULL)
    modelviewer = mv;
  KeepFocusOffTree keepFocus(fileTree);

  // A search is running now, so cancel any pending debounced one (Enter/Clear/timer all
  // funnel through here -- this stops a queued timer from re-searching the same text).
  m_searchTimer.Stop();

  if (m_mode == ViewerMode::Textures)
  {
    InitTextures();
    return;
  }

  // No client yet: there is no file index to list.
  if (!core::Game::instance().initDone())
  {
    clearRows();
    m_applied[slot(m_mode)].Clear();
    SetSearchStatus(_("Load a World of Warcraft client to browse its files."));
    return;
  }
  m_applied[slot(m_mode)] = txtContent->GetValue();
  const QString content = QString::fromWCharArray(txtContent->GetValue().c_str()).toLower().trimmed();
  if (reuseTree(treeState(), content))
    return;
  treeState().kept = false;
  if (m_mode == ViewerMode::Buildings)
    InitBuildings(content);
  else
    InitModels(content);
}

void FileControl::InitModels(const QString & content)
{
  LOG_INFO << "Initializing File Controls - Start";

  // The models whose name holds the search text, in one pass over the file index: the test
  // GameFolder::getFilteredFiles makes -- the ending first, then the search (none to make while browsing:
  // every name passes it). World models are the Buildings viewer's.
  std::set<GameFile *> files;
  const QRegularExpression m2Search("^.*" + content + ".*\\.m2");
  if (!m2Search.isValid())
    LOG_ERROR << m2Search.errorString();
  else
    for (auto it = GAMEDIRECTORY.begin(); it != GAMEDIRECTORY.end(); ++it)
    {
      const QString name = (*it)->name();
      if (!name.endsWith(QLatin1String(".m2")))
        continue;
      if (!content.isEmpty() && !name.contains(m2Search))
        continue;
      files.insert(*it);
    }

  LOG_INFO << "Initializing File Controls - Filtering done - files found" << files.size();

  // The raw character/ folder is replaced by the curated "Characters" race browser built below
  // (Playable / NPC), so the raw character/ entries are skipped here to avoid showing both.
  // Only substitute the race browser when NOT searching: during a search the
  // curated node ignores the query, so the raw character/ matches must remain
  // visible or character searches would return nothing.
  const bool buildRaceTree = content.isEmpty();

  // Build a fresh hierarchy and keep it on the control; the previous one is freed once its rows are gone
  // (clearRows, below).
  retireTree(m_modelsTree.root);
  m_modelsTree.openNodes.clear();
  m_modelsTree.topNode = nullptr;
  m_modelsTree.pickedNode = nullptr;
  m_modelsTree.root = new TreeStackItem();
  TreeStackItem & root = *m_modelsTree.root;
  size_t listed = 0;
  for (std::set<GameFile *>::iterator it = files.begin(); it != files.end(); ++it)
  {
    // fullname() may use '/' or '\\'; normalise like beautifyFileName before testing
    if (buildRaceTree && (*it)->fullname().toLower().replace('/', '\\').startsWith("character\\"))
      continue;

    QString name = (*it)->fullname();
    name += " [";
    name += QString::number((*it)->fileDataId());
    name += "]";

    beautifyFileName(name);

    QStringList Items = name.split("\\");
    TreeStackItem * curparent = &root;
    for(int i=0; i < Items.size() -1; i++)
    {
      TreeStackItem * child = curparent->getChildByName(Items[i]);
      if(!child)
      {
        child = new TreeStackItem();
        child->setName(Items[i]);
        curparent->addChild(child);
      }
      curparent = child;
    }
    TreeStackItem * child = new TreeStackItem();
    child->file = *it;
    child->setName(Items[Items.size()-1]);
    curparent->addChild(child);
    listed++;
  }

  // Add a race-categorised "Characters" section (Playable / NPC), driven by
  // ChrRaces, replacing the raw character/ folder skipped above.
  // Each race gets Male/Female leaves that point at the model GameFile, so they
  // load through the normal tree-selection path.
  if (buildRaceTree)
  {
    const auto raceMenu = RaceInfos::getRaceMenu();
    if (!raceMenu.empty())
    {
      TreeStackItem * charRaces = new TreeStackItem();
      charRaces->setName("Characters");
      TreeStackItem * playable = new TreeStackItem();
      playable->setName("Playable Races");
      TreeStackItem * npc = new TreeStackItem();
      npc->setName("NPC Races");
      charRaces->addChild(playable);
      charRaces->addChild(npc);

      for (const auto & e : raceMenu)
      {
        TreeStackItem * raceNode = new TreeStackItem();
        raceNode->setName(QString::fromStdString(e.name));

        bool hasModel = false;
        // sexID follows ChrModel.Sex: 0 male, 1 female. It is carried on the leaf because
        // several races can share one model file, and the file alone cannot say which race
        // (and so which set of customization options) was picked.
        struct SexLeaf { int fileID; const char * label; int sexID; };
        const SexLeaf sexes[2] = { { e.maleFileID, "Male", 0 }, { e.femaleFileID, "Female", 1 } };
        for (const auto & s : sexes)
        {
          if (s.fileID <= 0)
            continue;
          GameFile * f = GAMEDIRECTORY.getFile(s.fileID);
          if (!f)
            continue;
          TreeStackItem * leaf = new TreeStackItem();
          leaf->file = f;
          leaf->setName(s.label);
          leaf->raceID = e.raceID;
          leaf->sexID = s.sexID;
          raceNode->addChild(leaf);
          hasModel = true;
        }

        if (hasModel)
          (e.isNPC ? npc : playable)->addChild(raceNode);
        else
          delete raceNode;
      }

      root.addChild(charRaces);
    }
  }

  LOG_INFO << "Initializing File Controls - File Hierarchy created";

  // Populate the tree inside Freeze()/Thaw() to batch repaints. When browsing
  // (no search) populate LAZILY: only the top-level rows are added now, and each
  // branch's children are filled in when it is expanded (OnTreeItemExpanding).
  // Building all ~130k rows up front took ~9s and dominated startup. When a search
  // is active the result set is small, so populate eagerly and expand it.
  fileTree->Freeze();
  clearRows();
  root.id = fileTree->AddRoot(wxT("Root"));
  if (content.isEmpty())
  {
    root.appendChildren(fileTree);
  }
  else
  {
    root.createTreeItems(fileTree);
    fileTree->ExpandAll();
  }
  fileTree->Thaw();

  if (content.isEmpty())
    SetSearchStatus(wxEmptyString);
  else if (listed == 0)
    SetSearchStatus(_("No files match."));
  else
    SetSearchStatus(wxString::Format(listed == 1 ? _("%u file found") : _("%u files found"), (unsigned)listed));
  m_modelsTree.rootValid = true;
  m_modelsTree.rootContent = content;
  m_modelsTree.rootStatus = searchStatus->GetLabel();

  LOG_INFO << "Initializing File Controls - END";
}

// ---------------------------------------------------------------------------------------- Buildings mode
bool FileControl::ensureBuildings()
{
  if (m_buildings.built)
    return true;
  if (!core::Game::instance().initDone())
    return false;
  QElapsedTimer timer;
  timer.start();
  m_buildings = BuildingIndex();
  // The file index in path order: roots and groups by the rule in isWmoGroupFile, each group then tied to its
  // root (for a group's FileDataID typed in the search).
  const std::map<QString, GameFile *> & index = GAMEDIRECTORY.filesByPath();
  std::vector<std::pair<QString, GameFile *>> groups;
  std::map<QString, int> rootIndex;
  for (auto it = index.begin(); it != index.end(); ++it)
  {
    if (!it->first.endsWith(QLatin1String(".wmo"), Qt::CaseInsensitive))
      continue;
    m_buildings.wmoFiles++;
    if (isWmoGroupFile(it->first, index))
    {
      m_buildings.groupFiles++;
      groups.push_back(*it);
      continue;
    }
    rootIndex[it->first] = (int)m_buildings.roots.size();
    m_buildings.roots.push_back(it->second);
  }
  for (size_t i = 0; i < m_buildings.roots.size(); i++)
    if (m_buildings.roots[i]->fileDataId() > 0)
      m_buildings.byId.push_back(std::make_pair((int)m_buildings.roots[i]->fileDataId(), (int)i));
  std::sort(m_buildings.byId.begin(), m_buildings.byId.end());
  for (const auto & g : groups)
  {
    if (g.second->fileDataId() <= 0)
      continue;
    // "<root>_NNN[_lodN].wmo" -> "<root>.wmo" (the rule just matched it).
    QString stem = g.first.left(g.first.size() - 4);
    const int n = stem.size();
    if (n >= 5 && stem[n - 5] == QLatin1Char('_') && stem.mid(n - 4, 3).compare(QLatin1String("lod"), Qt::CaseInsensitive) == 0)
      stem.chop(5);
    stem.chop(4);
    auto root = rootIndex.find(stem + g.first.right(4));
    if (root != rootIndex.end())
      m_buildings.groupsById.push_back(std::make_pair((int)g.second->fileDataId(), root->second));
  }
  std::sort(m_buildings.groupsById.begin(), m_buildings.groupsById.end());
  m_buildings.built = true;
  LOG_INFO << "Buildings:" << (int)m_buildings.wmoFiles << ".wmo files," << (int)m_buildings.roots.size() << "roots,"
           << (int)m_buildings.groupFiles << "group files, listed in" << timer.elapsed() << "ms";
  return true;
}

QString FileControl::buildingLabel(GameFile * file)
{
  QString name = file->fullname();
  if (file->fileDataId() > 0)
    name += QString(" [%1]").arg(file->fileDataId());
  beautifyFileName(name);
  return name;
}

void FileControl::InitBuildings(const QString & content)
{
  if (!ensureBuildings())
  {
    clearRows();
    SetSearchStatus(_("Load a World of Warcraft client to browse its files."));
    return;
  }
  QElapsedTimer timer;
  timer.start();
  // The rows of a hierarchy made from a list of roots, labelled and nested as the Models tree's.
  auto hierarchyOf = [](const std::vector<GameFile *> & files) {
    TreeStackItem * root = new TreeStackItem();
    for (GameFile * file : files)
    {
      const QStringList items = buildingLabel(file).split("\\");
      TreeStackItem * parent = root;
      for (int i = 0; i < items.size() - 1; i++)
      {
        TreeStackItem * child = parent->getChildByName(items[i]);
        if (!child)
        {
          child = new TreeStackItem();
          child->setName(items[i]);
          parent->addChild(child);
        }
        parent = child;
      }
      TreeStackItem * leaf = new TreeStackItem();
      leaf->file = file;
      leaf->setName(items.last());
      parent->addChild(leaf);
    }
    return root;
  };

  fileTree->Freeze();
  // The hierarchy of the last search goes with its rows (the browsing one is kept).
  m_buildingsTree.openNodes.clear();
  m_buildingsTree.topNode = nullptr;
  m_buildingsTree.pickedNode = nullptr;
  if (m_buildingsTree.root != m_buildingsTree.browseRoot)
    retireTree(m_buildingsTree.root);
  m_buildingsTree.root = nullptr;
  clearRows();
  if (content.isEmpty())
  {
    // Browsing: the hierarchy of every root, made once per client and kept.
    if (!m_buildingsTree.browseRoot)
      m_buildingsTree.browseRoot = hierarchyOf(m_buildings.roots);
    m_buildingsTree.root = m_buildingsTree.browseRoot;
    m_buildingsTree.root->resetLoaded();
    m_buildingsTree.root->id = fileTree->AddRoot(wxT("Root"));
    m_buildingsTree.root->appendChildren(fileTree);
    fileTree->Thaw();
    SetSearchStatus(m_buildings.roots.empty() ? wxString(_("This client has no buildings.")) : wxString());
  }
  else
  {
    // Searching: every word of the search in the path (a name is part of its path), and a number is also the
    // FileDataID of a building, or of a group file -- found as its building -- or one to look up.
    QString term = content;
    term.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList words = term.split(QLatin1Char(' '), QString::SkipEmptyParts);
    std::vector<GameFile *> found;
    size_t matched = 0;
    for (GameFile * file : m_buildings.roots)
    {
      const QString path = file->fullname().toLower().replace(QLatin1Char('\\'), QLatin1Char('/'));
      bool all = true;
      for (const QString & word : words)
        all = all && path.contains(word);
      if (!all)
        continue;
      if (matched++ < BuildingsShown)
        found.push_back(file);
    }
    m_buildingsTree.root = hierarchyOf(found);
    TreeStackItem & root = *m_buildingsTree.root;
    root.id = fileTree->AddRoot(wxT("Root"));
    root.createTreeItems(fileTree);
    fileTree->ExpandAll();
    // The FileDataID first, as Textures has it.
    wxString idNote;
    const bool pinned = pinFileDataIdRow(term, idNote);
    fileTree->Thaw();
    wxString status;
    if (matched == 0)
      status = pinned ? wxString() : wxString(_("No buildings match."));
    else if (matched > found.size())
      status = wxString::Format(_("%u of %u buildings shown -- type more to narrow the search"), (unsigned)found.size(),
                                (unsigned)matched);
    else
      status = wxString::Format(matched == 1 ? _("%u building found") : _("%u buildings found"), (unsigned)matched);
    if (!idNote.IsEmpty())
      status = status.IsEmpty() ? idNote : idNote + wxT("\n") + status;
    SetSearchStatus(status);
  }
  m_buildingsTree.rootValid = true;
  m_buildingsTree.rootContent = content;
  m_buildingsTree.rootStatus = searchStatus->GetLabel();
  LOG_INFO << "Buildings: tree made in" << timer.elapsed() << "ms";
}

bool FileControl::pinFileDataIdRow(const QString & term, wxString & note)
{
  note.Clear();
  // A number: ASCII digits only, in range, not 0.
  bool digits = !term.isEmpty();
  for (const QChar c : term)
    digits = digits && c >= QLatin1Char('0') && c <= QLatin1Char('9');
  bool ok = false;
  const int id = digits ? term.toInt(&ok) : 0;
  if (!ok || id <= 0 || !core::Game::instance().folder().clientProfile().hasFileDataId || !fileTree->GetRootItem().IsOk())
    return false;
  const wxTreeItemId root = fileTree->GetRootItem();
  auto byId = std::lower_bound(m_buildings.byId.begin(), m_buildings.byId.end(), std::make_pair(id, -1));
  auto group = std::lower_bound(m_buildings.groupsById.begin(), m_buildings.groupsById.end(), std::make_pair(id, -1));
  if (byId != m_buildings.byId.end() && byId->first == id)
  {
    GameFile * file = m_buildings.roots[byId->second];
    fileTree->PrependItem(root, buildingLabel(file).toStdWString(), -1, -1, new FileTreeData(file, nullptr));
  }
  else if (group != m_buildings.groupsById.end() && group->first == id)
  {
    GameFile * file = m_buildings.roots[group->second];
    fileTree->PrependItem(root, buildingLabel(file).toStdWString(), -1, -1, new FileTreeData(file, nullptr));
    note = wxString::Format(_("FileDataID %d is a group file of this building."), id);
  }
  else
  {
    FileTreeData * lookup = new FileTreeData(nullptr, nullptr);
    lookup->lookupFileDataId = id;
    fileTree->PrependItem(root, wxString::Format(_("Look up FileDataID %d"), id), -1, -1, lookup);
  }
  return true;
}

void FileControl::SelectBuildingRow(wxTreeItemId item)
{
  FileTreeData * data = item.IsOk() ? dynamic_cast<FileTreeData *>(fileTree->GetItemData(item)) : nullptr;
  if (!data || !modelviewer)
    return;
  if (data->lookupFileDataId > 0)
  {
    // The building on screen looked up again (the click's double-click, Enter again): nothing to load.
    if (modelviewer->canvasHasWorldModel() && modelviewer->canvas->wmo->ok &&
        (int)modelviewer->canvas->wmo->fileDataID == data->lookupFileDataId)
      return;
    LookUpBuilding(data->lookupFileDataId);
    return;
  }
  if (!data->file)
    return;   // a folder
  // The building on screen picked again (Enter on its row): nothing to load.
  if (isLoaded(data->file) && modelviewer->canvas->wmo->ok)
    return;
  SelectWMOFile(data->file);
}

void FileControl::LookUpBuilding(int fileDataId)
{
  // The file by its FileDataID (the index opens one the listfile does not name), then its first chunks: a root
  // is MVER then MOHD, a group file MVER then MOGP.
  GameFile * file = GAMEDIRECTORY.getFile(fileDataId);
  if (!file)
  {
    SetSearchStatus(wxString::Format(_("FileDataID %d is not in the loaded client."), fileDataId));
    return;
  }
  QString kind;
  if (!file->isCurrentlyOpen() && file->open(false))
  {
    unsigned char head[20] = { 0 };
    const size_t got = file->read(head, sizeof(head));
    file->close();
    auto tag = [&head](int at) { return QString::fromLatin1((const char *)head + at, 4); };
    if (got == 0)
      kind = "unread";   // an encrypted file with no key in the client opens, but reads nothing
    else if (got < sizeof(head))
      kind = "short";
    else if (tag(0) == "REVM" && tag(12) == "DHOM")
      kind = "root";
    else if (tag(0) == "REVM" && tag(12) == "PGOM")
      kind = "group";
    else
      kind = "other";
  }
  else
    kind = "unread";
  LOG_INFO << "Buildings: FileDataID" << fileDataId << "looked up as" << file->fullname() << "--" << kind;
  if (kind == "root")
  {
    SelectWMOFile(file);
    return;
  }
  if (kind == "group")
    SetSearchStatus(wxString::Format(_("FileDataID %d is a world model group file; it is loaded with its building."),
                                     fileDataId));
  else if (kind == "unread")
    SetSearchStatus(wxString::Format(_("FileDataID %d could not be read (it may be encrypted, or not downloaded yet)."),
                                     fileDataId));
  else
    SetSearchStatus(wxString::Format(_("FileDataID %d is not a world model."), fileDataId));
}

// Lazy tree fill-in: when a collapsed branch is expanded, add its direct children
// to the wxTreeCtrl (idempotent -- appendChildren no-ops once a node is loaded).
void FileControl::OnTreeItemExpanding(wxTreeEvent &event)
{
  const wxTreeItemId item = event.GetItem();
  if (!item.IsOk())
    return;
  if (m_mode == ViewerMode::Textures)
  {
    m_textures->expanding(item);
    return;
  }
  FileTreeData * data = (FileTreeData *)fileTree->GetItemData(item);
  if (data && data->node)
    data->node->appendChildren(fileTree);
}

// copy from ModelOpened::Export
void FileControl::Export(GameFile * f, int select)
{
  // The row's own file (not looked up again by its name, which a file known only by its FileDataID cannot be).
  if (!f)
    return;
  const wxString val(f->fullname().toStdWString());

  f->open();

  if (f->isEof())
  {
    LOG_ERROR << "Could not extract" << QString::fromWCharArray(val.c_str());
    f->close();
    return;
  }

  LOG_INFO << "Saving" << QString::fromWCharArray(val.c_str());

  wxFileName fn(val);

  FILE *hFile = NULL;
  wxString filename;
  if (select == 1)
  {
    filename = wxFileSelector(wxT("Save..."), wxGetCwd(), fn.GetName(), fn.GetExt(), fn.GetExt().Upper()+wxT(" Files (.")+fn.GetExt().Lower()+wxT(")|*.")+fn.GetExt().Lower());
  }
  else
  {
    filename = wxGetCwd()+SLASH+wxT("Export")+SLASH+fn.GetFullName();
  }

  LOG_INFO << "Saving to" << QString::fromWCharArray(filename.c_str());

  if ( !filename.empty() )
  {
    hFile = fopen(filename.mb_str(), "wb");
  }

  if (hFile)
  {
    fwrite(f->getBuffer(), 1, f->getSize(), hFile);
    fclose(hFile);
  }
  else
  {
    LOG_ERROR << "Saving to" << QString::fromWCharArray(filename.c_str()) << "failed";
  }

  f->close();
}

void FileControl::OnPopupClick(wxCommandEvent &evt)
{
  FileTreeData *data = (FileTreeData*)(static_cast<wxMenu *>(evt.GetEventObject())->GetClientData());
  if (!data || !data->file)
    return;

  if (evt.GetId() == ID_FILELIST_SAVE)
    Export(data->file, 1);
}

void FileControl::OnTreeMenu(wxTreeEvent &event)
{
  wxTreeItemId item = event.GetItem();

  if (!item.IsOk() || !modelviewer->canvas) // make sure that a valid Tree Item was actually selected.
    return;

  if (m_mode == ViewerMode::Textures)
  {
    ShowTextureMenu(item);
    return;
  }

  void *data = reinterpret_cast<void *>(fileTree->GetItemData(item));
  FileTreeData *tdata = (FileTreeData*)data;

  // make sure the data (file name) is valid: folder rows carry no file
  if (!data || !tdata->file)
    return; // isn't valid, exit.

  // Make a menu to save the file as it is
  wxMenu infoMenu;
  infoMenu.SetClientData( data );
  infoMenu.Append(ID_FILELIST_SAVE, wxT("&Save..."), wxT("Save this object"));

  infoMenu.Connect(wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction)&FileControl::OnPopupClick, NULL, this);
  PopupMenu(&infoMenu);
}

void FileControl::ClearCanvas()
{
  if (!modelviewer->isModel && !modelviewer->isWMO)
    return;

  // Delete any previous models that were loaded.
  if (modelviewer->isWMO) {
    // Detaches canvas->root and clears g_selWMO before the delete (it used to leave both dangling).
    modelviewer->canvas->ClearWMO();
  } else if (modelviewer->isModel) {
    modelviewer->canvas->clearAttachments();

    // If it was a character model, no need to delete canvas->model, 
    //it was just pointing to a model created as an attachment - just set back to NULL instead.
    //canvas->model = NULL;
/*
    if (!modelviewer->isChar) { 
      
      modelviewer->canvas->model = NULL;
    } else{
      modelviewer->charControl->charAtt = NULL;

      wxString rootfn(data->fn);
      if (rootfn.Last() != '2' && modelviewer->canvas->model) {
        modelviewer->canvas->model = NULL;
      }
    }
*/
    //wxDELETE(modelviewer->canvas->model); // may memory leak
    modelviewer->canvas->setModel(NULL);
    // Nothing goes on pointing at the model just deleted -- the character controls, the Attachments window, the
    // Animation panel -- whatever is loaded next (a world model sets none of them again).
    modelviewer->charControl->charAtt = NULL;
    modelviewer->charControl->model = NULL;
    if (modelviewer->modelControl)
      modelviewer->modelControl->Forget();
    if (modelviewer->animControl)
      modelviewer->animControl->Forget();
    g_selModel = NULL;
  }

#ifdef _DEBUG
  GLenum err=glGetError();
  if (err)
    LOG_ERROR << "An OpenGL error occured." << err;
  LOG_INFO << "Clearing textures from previous model...";
#endif
  // Texture clearing and debugging
  TEXTUREMANAGER.clear();

#ifdef _DEBUG
  err = glGetError();
  if (err)
    LOG_ERROR << "An OpenGL error occured." << err;
#endif

  modelviewer->isModel = false;
  modelviewer->isChar = false;
  modelviewer->isWMO = false;
}

void FileControl::UpdateInterface()
{
  // Disable whatever formats can't be export yet.

  // Don't run if there aren't any models loaded!
  if (modelviewer == NULL)
    return;

  // You MUST put true in one if the other is false! Otherwise, if they open the other model type and go back,
  // your function will still be disabled!!
  // A model kept loaded behind another viewer is not on screen: its character commands wait until it is.
  if (modelviewer->isModel == true && modelviewer->isModelsMode()){
    // If it's an M2 file...
    // Enable Controls for Characters
    modelviewer->charMenu->Enable(ID_SAVE_CHAR, true);
    modelviewer->charMenu->Enable(ID_SHOW_UNDERWEAR, true);
    modelviewer->charMenu->Enable(ID_SHOW_EARS, true);
    modelviewer->charMenu->Enable(ID_SHOW_HAIR, true);
    modelviewer->charMenu->Enable(ID_SHOW_FACIALHAIR, true);
    modelviewer->charMenu->Enable(ID_SHOW_FEET, true);
    modelviewer->charMenu->Enable(ID_SHEATHE, true);
    modelviewer->charMenu->Enable(ID_SAVE_EQUIPMENT, true);
    modelviewer->charMenu->Enable(ID_LOAD_EQUIPMENT, true);
    modelviewer->charMenu->Enable(ID_CLEAR_EQUIPMENT, true);
    modelviewer->charMenu->Enable(ID_LOAD_SET, true);
    modelviewer->charMenu->Enable(ID_LOAD_START, true);
    modelviewer->charMenu->Enable(ID_MOUNT_CHARACTER, true);
    modelviewer->charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, true);
  }else if (modelviewer->isWMO == true){
    // If the object is a WMO file...
    modelviewer->charMenu->Enable(ID_SAVE_CHAR, false);
    modelviewer->charMenu->Enable(ID_SHOW_UNDERWEAR, false);
    modelviewer->charMenu->Enable(ID_SHOW_EARS, false);
    modelviewer->charMenu->Enable(ID_SHOW_HAIR, false);
    modelviewer->charMenu->Enable(ID_SHOW_FACIALHAIR, false);
    modelviewer->charMenu->Enable(ID_SHOW_FEET, false);
    modelviewer->charMenu->Enable(ID_SHEATHE, false);
    modelviewer->charMenu->Enable(ID_SAVE_EQUIPMENT, false);
    modelviewer->charMenu->Enable(ID_LOAD_EQUIPMENT, false);
    modelviewer->charMenu->Enable(ID_CLEAR_EQUIPMENT, false);
    modelviewer->charMenu->Enable(ID_LOAD_SET, false);
    modelviewer->charMenu->Enable(ID_LOAD_START, false);
    modelviewer->charMenu->Enable(ID_MOUNT_CHARACTER, false);
    modelviewer->charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);
  }else{
    // If it's not a 3D file...
    modelviewer->charMenu->Enable(ID_SAVE_CHAR, false);
    modelviewer->charMenu->Enable(ID_SHOW_UNDERWEAR, false);
    modelviewer->charMenu->Enable(ID_SHOW_EARS, false);
    modelviewer->charMenu->Enable(ID_SHOW_HAIR, false);
    modelviewer->charMenu->Enable(ID_SHOW_FACIALHAIR, false);
    modelviewer->charMenu->Enable(ID_SHOW_FEET, false);
    modelviewer->charMenu->Enable(ID_SHEATHE, false);
    modelviewer->charMenu->Enable(ID_SAVE_EQUIPMENT, false);
    modelviewer->charMenu->Enable(ID_LOAD_EQUIPMENT, false);
    modelviewer->charMenu->Enable(ID_CLEAR_EQUIPMENT, false);
    modelviewer->charMenu->Enable(ID_LOAD_SET, false);
    modelviewer->charMenu->Enable(ID_LOAD_START, false);
    modelviewer->charMenu->Enable(ID_MOUNT_CHARACTER, false);
    modelviewer->charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);
  }
  // Randomise and eye glow (LoadModel enables them for a character) wait in the other viewers too.
  if (!modelviewer->isModelsMode())
  {
    modelviewer->charMenu->Enable(ID_CHAR_RANDOMISE, false);
    modelviewer->charMenu->Enable(ID_CHAREYEGLOW, false);
  }
  else if (modelviewer->isModel && modelviewer->isChar)
  {
    modelviewer->charMenu->Enable(ID_CHAR_RANDOMISE, true);
    modelviewer->charMenu->Enable(ID_CHAREYEGLOW, true);
  }

  // The Model panel follows whatever was just opened (a model, a WMO, nothing).
  modelviewer->DisplayedContentChanged();

  // Update the layout -- only if a pane's shown state changed. This runs after every selection
  // in the file list, and an unconditional Update() blinked the whole window each time: see
  // ModelViewer::CommitLayoutIfChanged.
  modelviewer->CommitLayoutIfChanged();
}

void FileControl::OnTreeSelect(wxTreeEvent &event)
{
  wxTreeItemId item = event.GetItem();

  // make sure that a valid Tree Item was actually selected; a row picked again by the tree coming back as
  // it was (reuseModelTree) is not a new pick.
  if (!item.IsOk() || !modelviewer->canvas || m_restoringTree){
    return;
  }

  if (m_mode == ViewerMode::Textures)
  {
    SelectTextureRow(item);
    return;
  }
  if (m_mode == ViewerMode::Buildings)
  {
    // Rows passed with an arrow key held down are not loaded: the one it stops on is, when it is let go.
    if (m_keysRepeating)
    {
      m_buildingPickPending = true;
      return;
    }
    SelectBuildingRow(item);
    return;
  }

  FileTreeData *data = (FileTreeData*)fileTree->GetItemData(item);

  // make sure the data (file name) is valid
  if (!data || !data->file){
    return; // isn't valid, exit.
  }

  CurrentItem = item;

  // A model row loads the model (a race-browser leaf names the race and sex it stands for; an ordinary file
  // row does not).
  SelectModelFile(data->file,
                  data->node ? data->node->raceID : -1,
                  data->node ? data->node->sexID : -1);
}

bool FileControl::isLoaded(GameFile * file) const
{
  if (!file || !modelviewer || !modelviewer->canvas)
    return false;
  if (m_mode == ViewerMode::Buildings)
    return modelviewer->canvasHasWorldModel() &&
           (modelviewer->canvas->wmo->itemName().compare(file->fullname(), Qt::CaseInsensitive) == 0 ||
            (file->fileDataId() > 0 && (int)modelviewer->canvas->wmo->fileDataID == file->fileDataId()));
  const WoWModel * model = modelviewer->canvasHasModel() ? modelviewer->canvas->model() : nullptr;
  return model && model->gamefile == file;
}

void FileControl::SelectModelFile(GameFile * file, int raceID, int sexID)
{
  if (!file || !modelviewer || !modelviewer->canvas)
    return;
  wxString rootfn(file->fullname().toStdWString());
  // Exit, if its the same model thats currently loaded -- unless a different race was picked on
  // it: races that share a model file (Mag'har Orc and Orc) are the same file but not the same
  // character, and the pick has to go through so the race can change.
  const WoWModel * loaded = modelviewer->canvas->model();
  const bool sameFile = loaded && !loaded->name().isEmpty() &&
                        loaded->name().toStdWString() == std::wstring(rootfn.c_str());
  const bool sameRace = (raceID < 0) || (loaded && loaded->infos.raceID == raceID && loaded->infos.sexID == sexID);
  if (sameFile && sameRace)
    return; // clicked on the same model thats currently loaded, no need to load it again - exit

  ClearCanvas();
  LOG_INFO << "Selecting model in tree selector:" << QString::fromWCharArray(rootfn.c_str());

  // Check to make sure the selected item is a model (an *.m2 file).
  modelviewer->isModel = (rootfn.Last() == '2');

  // not functional yet.
  //if (wxGetKeyState(WXK_SHIFT))
  //  canvas->AddModel(rootfn);
  //else
  modelviewer->LoadModel(GAMEDIRECTORY.getFile(QString::fromWCharArray(rootfn.c_str())), raceID, sexID);  // Load the model.

  UpdateInterface();
}

void FileControl::SelectWMOFile(GameFile * file)
{
  if (!file || !modelviewer || !modelviewer->canvas)
    return;
  // A world model is the Buildings viewer's: one loaded from anywhere else (the command line, the self-test)
  // switches to it first, so the switch and the load are one change on screen. (From the Buildings tree it
  // already is.)
  modelviewer->SetViewerMode(ViewerMode::Buildings);
  ClearCanvas();

  modelviewer->isWMO = true;
  wxString rootfn(file->fullname().toStdWString());

  //modelviewer->canvas->model->modelType = MT_WMO;

  // THE PICKED FILE IS TRIED AS A ROOT FIRST. Whether a .wmo is a root is its MOHD chunk, not its name:
  // real roots are named "<name>_NNN.wmo" (11xt_rockbridge_003.wmo, FileDataID 5569224) with no
  // "<name>.wmo" beside them, and guessing from the name first turned such a root into a file that does
  // not exist. Groups belong to a root through its GFID list, never through names.
  int rootFileDataID = file->fileDataId() > 0 ? file->fileDataId() : 0;
  modelviewer->canvas->LoadWMO(rootfn, rootFileDataID);

  // Only a file that turned out not to be a root (no MOHD) and is named like a group file falls back to the
  // old name rule, "<name>_NNN.wmo" -> "<name>.wmo": a convenience for a group file picked by hand (Browse
  // hides them), not how groups are found. The group index the old code took from the name is not kept: it
  // was read after the suffix had been cut off, i.e. from the root's own name, so the label is the root's (-1).
  const size_t len = rootfn.length();
  const bool groupName = len > 8 && rootfn[len - 8] == '_' && rootfn[len - 7] >= '0' && rootfn[len - 7] <= '9';
  if (groupName && (!modelviewer->canvas->wmo || !modelviewer->canvas->wmo->ok))
  {
    wxString stripped = rootfn.Left(len - 8) + wxT(".wmo");
    GameFile * rootFile = GAMEDIRECTORY.getFile(QString::fromWCharArray(stripped.c_str()));
    rootFileDataID = (rootFile && rootFile->fileDataId() > 0) ? rootFile->fileDataId() : 0;
    LOG_INFO << __FUNCTION__ << QString::fromWCharArray(rootfn.c_str()) << "is not a root WMO; trying"
             << QString::fromWCharArray(stripped.c_str());
    rootfn = stripped;
    modelviewer->canvas->LoadWMO(rootfn, rootFileDataID);
  }
  const int id = -1;

  LOG_INFO << __FUNCTION__ << "wmo =" << modelviewer->canvas->wmo << "root FileDataID" << rootFileDataID;

  // No wmo->loadGroup(id) any more: it rebuilt every group a second time (after the constructor's own
  // build) purely for the hidden OpenGL canvas, and a metadata-only root has no groups to build.
  modelviewer->animControl->UpdateWMO(modelviewer->canvas->wmo, id);

  // The Unity player is told before the viewport decides (UpdateInterface), as ModelViewer::LoadModel
  // does for a model; with no player ready yet, onUnityReady sends it.
  modelviewer->SendLoadToUnity();

  UpdateInterface();
}

// Enter or the search button runs the search at any length; the cancel button clears it and
// brings the browse tree back.
void FileControl::OnButton(wxCommandEvent &event)
{
  if (event.GetEventType() == wxEVT_SEARCH_CANCEL)
    txtContent->SetValue(wxEmptyString);
  Init();
  // Buildings: Enter on a FileDataID opens that building (its row, the row of the building a group file belongs
  // to, or the row that looks it up), picked first.
  if (ShowsBuildings() && event.GetEventType() == wxEVT_SEARCH)
  {
    const QString term = QString::fromWCharArray(txtContent->GetValue().c_str()).trimmed();
    bool digits = !term.isEmpty();
    for (const QChar c : term)
      digits = digits && c.isDigit();
    wxTreeItemIdValue cookie;
    const wxTreeItemId first = fileTree->GetRootItem().IsOk() ? fileTree->GetFirstChild(fileTree->GetRootItem(), cookie)
                                                               : wxTreeItemId();
    FileTreeData * data = first.IsOk() ? dynamic_cast<FileTreeData *>(fileTree->GetItemData(first)) : nullptr;
    if (digits && data && !data->node && (data->file || data->lookupFileDataId > 0))
    {
      if (fileTree->GetSelection() == first)
        SelectBuildingRow(first);
      else
        fileTree->SelectItem(first);
    }
  }
  // Textures: Enter on a FileDataID opens that file (its texture row, or the row that looks it up).
  if (ShowsTextures() && event.GetEventType() == wxEVT_SEARCH)
  {
    const QString term = QString::fromWCharArray(txtContent->GetValue().c_str()).trimmed();
    bool digits = !term.isEmpty();
    for (const QChar c : term)
      digits = digits && c.isDigit();
    wxTreeItemIdValue cookie;
    const wxTreeItemId first = fileTree->GetRootItem().IsOk() ? fileTree->GetFirstChild(fileTree->GetRootItem(), cookie)
                                                               : wxTreeItemId();
    TextureEntry entry;
    bool lookup = false;
    if (digits && first.IsOk() && m_textures->textureOf(first, entry, lookup) && entry.fileDataId == term.toInt())
    {
      fileTree->SelectItem(first);
      SelectTextureRow(first);
    }
  }
}

// Fires on every keystroke in the search box. Rather than searching immediately (the filter
// scans ~130k files and rebuilds the tree, far too heavy to run per key), restart a short
// one-shot timer; the search runs in OnSearchTimer once typing pauses. EVT_TEXT also fires on
// programmatic SetValue (e.g. the Clear button), but those paths call Init() -> Stop() so the
// queued timer is harmlessly cancelled.
void FileControl::OnSearchText(wxCommandEvent &event)
{
  m_searchTimer.Start(300, wxTIMER_ONE_SHOT);

  const QString term = QString::fromWCharArray(txtContent->GetValue().c_str()).trimmed();
  if (!term.isEmpty() && term.length() < 3)
    SetSearchStatus(_("Type 3 or more characters, or press Enter."));
}

void FileControl::OnSearchTimer(wxTimerEvent &event)
{
  const QString term = QString::fromWCharArray(txtContent->GetValue().c_str()).trimmed();

  // Auto-search only once the term is selective enough; a 1-2 char term matches a huge slice
  // of the archive and would rebuild a massive tree for no useful result. An empty box
  // restores the default browse tree. (Enter still forces a search at any length.)
  if (term.isEmpty() || term.length() >= 3)
    Init();
}

// ------------------------------------------------------------------------------- the viewer mode
void FileControl::FollowViewerMode(ViewerMode mode)
{
  if (mode == m_mode)
    return;
  // The tree left is kept for its return: the texture tree's open folders, top row and picked row; the
  // model or building tree itself (keepTree).
  if (m_mode == ViewerMode::Textures)
    m_textures->rememberOpenFolders();
  else
    keepTree(treeState());
  // Each mode's search is what its tree shows, not text typed and not run: that one could be a
  // 1-character search over every model, which takes seconds.
  m_search[slot(m_mode)] = m_applied[slot(m_mode)];
  txtContent->ChangeValue(m_search[slot(mode)]);
  m_mode = mode;
  m_keysRepeating = false;
  m_buildingPickPending = false;
  UpdateSearchHint();
  Init();
}

void FileControl::PickedRowFollowsLoad()
{
  // A model loaded from a menu, a saved character or an import has no row here, and the row picked before
  // would claim it -- and a click on that row would do nothing, being picked already.
  if (m_mode == ViewerMode::Textures || !fileTree)
    return;
  const wxTreeItemId picked = fileTree->GetSelection();
  FileTreeData * data = picked.IsOk() ? dynamic_cast<FileTreeData *>(fileTree->GetItemData(picked)) : nullptr;
  if (!data || !data->file || isLoaded(data->file))
    return;
  m_restoringTree = true;
  fileTree->Unselect();
  m_restoringTree = false;
}

void FileControl::KeysRepeating(bool repeating)
{
  if (modelviewer && modelviewer->textureView)
    modelviewer->textureView->setKeysRepeating(repeating && m_mode == ViewerMode::Textures);
  m_keysRepeating = repeating && m_mode == ViewerMode::Buildings;
  // Let go: the row the key stopped on is loaded.
  if (!m_keysRepeating && m_buildingPickPending)
  {
    m_buildingPickPending = false;
    if (m_mode == ViewerMode::Buildings)
      SelectBuildingRow(fileTree->GetSelection());
  }
}

void FileControl::keepTree(TreeState & state)
{
  // Only the tree built for this search, and only while that hierarchy is the client's.
  state.kept = false;
  state.openNodes.clear();
  state.topNode = nullptr;
  state.pickedNode = nullptr;
  state.pickedPinned = false;
  if (!state.root || !state.rootValid || !fileTree->GetRootItem().IsOk())
    return;
  std::function<void(wxTreeItemId)> collect = [&](wxTreeItemId parent) {
    wxTreeItemIdValue cookie;
    for (wxTreeItemId c = fileTree->GetFirstChild(parent, cookie); c.IsOk(); c = fileTree->GetNextChild(parent, cookie))
    {
      FileTreeData * data = dynamic_cast<FileTreeData *>(fileTree->GetItemData(c));
      if (!data || !data->node || !fileTree->IsExpanded(c))
        continue;
      state.openNodes.insert(data->node);
      collect(c);
    }
  };
  collect(fileTree->GetRootItem());
  const wxTreeItemId top = fileTree->GetFirstVisibleItem();
  FileTreeData * topData = top.IsOk() ? dynamic_cast<FileTreeData *>(fileTree->GetItemData(top)) : nullptr;
  state.topNode = topData ? topData->node : nullptr;
  const wxTreeItemId picked = fileTree->GetSelection();
  FileTreeData * pickedData = picked.IsOk() ? dynamic_cast<FileTreeData *>(fileTree->GetItemData(picked)) : nullptr;
  state.pickedNode = pickedData ? pickedData->node : nullptr;
  state.pickedPinned = pickedData && !pickedData->node && pickedData->file;
  state.kept = true;
}

void FileControl::reopenRows(TreeState & state, wxTreeItemId parent, wxTreeItemId & top, wxTreeItemId & picked)
{
  wxTreeItemIdValue cookie;
  for (wxTreeItemId c = fileTree->GetFirstChild(parent, cookie); c.IsOk(); c = fileTree->GetNextChild(parent, cookie))
  {
    FileTreeData * data = dynamic_cast<FileTreeData *>(fileTree->GetItemData(c));
    if (!data || !data->node)
      continue;
    if (data->node == state.topNode)
      top = c;
    if (data->node == state.pickedNode)
      picked = c;
    if (state.openNodes.count(data->node))
    {
      fileTree->Expand(c);   // filled by OnTreeItemExpanding
      reopenRows(state, c, top, picked);
    }
  }
}

bool FileControl::reuseTree(TreeState & state, const QString & content)
{
  if (!state.kept || !state.root || !state.rootValid || state.rootContent != content)
    return false;
  state.kept = false;
  QElapsedTimer timer;
  timer.start();
  TreeStackItem & root = *state.root;
  root.resetLoaded();
  fileTree->Freeze();
  clearRows();
  root.id = fileTree->AddRoot(wxT("Root"));
  wxTreeItemId top, picked;
  // (A search's rows are all made at once; the folders open again are the ones that were open.)
  if (content.isEmpty())
    root.appendChildren(fileTree);
  else
    root.createTreeItems(fileTree);
  reopenRows(state, root.id, top, picked);
  // A Buildings search of a FileDataID: its pinned row again, first (it is no part of the hierarchy).
  if (&state == &m_buildingsTree && !content.isEmpty())
  {
    wxString note;
    QString term = content;
    term.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (pinFileDataIdRow(term, note) && state.pickedPinned)
    {
      wxTreeItemIdValue cookie;
      picked = fileTree->GetFirstChild(root.id, cookie);
    }
  }
  // The row picked, picked again -- while its model is still the one loaded (a model loaded from a menu
  // meanwhile has no row here) -- without loading it again; then the same row at the top.
  if (picked.IsOk() && isLoaded(static_cast<FileTreeData *>(fileTree->GetItemData(picked))->file))
  {
    m_restoringTree = true;
    fileTree->SelectItem(picked);
    m_restoringTree = false;
  }
  if (top.IsOk())
    fileTree->ScrollTo(top);
  fileTree->Thaw();
  SetSearchStatus(state.rootStatus);
  LOG_INFO << "Initializing File Controls - the tree kept, rows again in" << timer.elapsed() << "ms,"
           << (int)state.openNodes.size() << "folders opened again";
  return true;
}

void FileControl::forgetTrees()
{
  for (TreeState * state : { &m_modelsTree, &m_buildingsTree })
  {
    state->kept = false;
    state->openNodes.clear();
    state->topNode = nullptr;
    state->pickedNode = nullptr;
    state->rootValid = false;
    // The old client's files: retired, and freed by the next rebuild of the tree (the rows on screen may still
    // point into them until then).
    retireTree(state->root);
    if (state->browseRoot != state->root)
      retireTree(state->browseRoot);
    state->root = nullptr;
    state->browseRoot = nullptr;
  }
  m_buildings = BuildingIndex();
  // Textures mode's rows point into no hierarchy: nothing needs to wait for the next rebuild of a tree.
  if (m_mode == ViewerMode::Textures)
    freeRetiredLater();
}

void FileControl::retireTree(TreeStackItem * root)
{
  if (root && std::find(m_retired.begin(), m_retired.end(), root) == m_retired.end())
    m_retired.push_back(root);
}

void FileControl::clearRows()
{
  fileTree->DeleteAllItems();
  freeRetiredLater();
}

void FileControl::freeRetiredLater()
{
  if (m_retired.empty())
    return;
  // Exactly the hierarchies retired until now: no row points into them any more. One retired after this call (a
  // client change while the old rows are still on screen) waits for its own rows to go.
  std::vector<TreeStackItem *> batch;
  batch.swap(m_retired);
  CallAfter([this, batch]() { freeTrees(batch); });
}

void FileControl::freeTrees(const std::vector<TreeStackItem *> & roots)
{
  QElapsedTimer timer;
  timer.start();
  size_t freed = 0;
  for (TreeStackItem * root : roots)
  {
    // Never one a tree still keeps (a defence: retireTree is only given hierarchies that were let go of).
    if (root == m_modelsTree.root || root == m_buildingsTree.root || root == m_modelsTree.browseRoot ||
        root == m_buildingsTree.browseRoot)
      continue;
    delete root; // the whole subtree: each node owns its children
    freed++;
  }
  LOG_INFO << "Browse: freed" << (unsigned int)freed << "tree hierarchies the tree no longer shows in" << timer.elapsed() << "ms";
}

// ---------------------------------------------------------------------------------------- Textures mode
// The client's textures from the texture catalogue (TextureBrowse), in this tree, with this search
// box: see TextureBrowse.h for why not the hierarchy the models' tree is built from.

bool FileControl::ShowsTextures() const
{
  return m_mode == ViewerMode::Textures;
}

bool FileControl::ShowsBuildings() const
{
  return m_mode == ViewerMode::Buildings;
}

void FileControl::InitTextures()
{
  const QString term = QString::fromWCharArray(txtContent->GetValue().c_str()).trimmed();
  if (!m_textures->ensureCatalog())
  {
    m_textures->clear();
    m_applied[slot(ViewerMode::Textures)].Clear();
    SetSearchStatus(UnityAssetAccess::isClientLoading() ? _("Loading the game client...")
                                                         : _("Load a World of Warcraft client to browse its files."));
    return;
  }
  const bool fromSearch = m_textures->showsSearch();
  m_applied[slot(ViewerMode::Textures)] = txtContent->GetValue();
  SetSearchStatus(m_textures->populate(term));

  // Browsing again after a search: open the folders down to the texture on screen, so a texture found by
  // searching is found in its place too. Coming back from the models, the tree is as it was left instead
  // (populate opens the same folders, at the same row, with the same row picked).
  const bool selected = modelviewer && modelviewer->textureView &&
                        modelviewer->textureView->selection() != TextureView::Selection::None;
  if (term.isEmpty() && selected && fromSearch)
  {
    const TextureView::Current & shown = modelviewer->textureView->current();
    if (!shown.entry.path.isEmpty() || shown.entry.fileDataId > 0)
      m_textures->reveal(shown.entry, true);
  }
}

void FileControl::SelectTextureRow(wxTreeItemId item)
{
  if (m_textures->busy() || !modelviewer)
    return;
  TextureEntry entry;
  bool lookup = false;
  if (!m_textures->textureOf(item, entry, lookup))
    return;   // a folder, a range or a group: nothing to show
  // The selection only: Textures mode already has its menus and panels (nothing else to update).
  modelviewer->ShowTexture(entry, lookup);
}

void FileControl::ShowTextureMenu(wxTreeItemId item)
{
  TextureEntry entry;
  bool lookup = false;
  if (!modelviewer || !modelviewer->textureView || !m_textures->textureOf(item, entry, lookup))
    return;
  enum { ID_TEX_PNG = wxID_HIGHEST + 6400, ID_TEX_BLP, ID_TEX_COPY_PATH, ID_TEX_COPY_ID };
  wxMenu menu;
  menu.Append(ID_TEX_PNG, _("Export PNG..."));
  menu.Append(ID_TEX_BLP, _("Export original BLP..."));
  menu.AppendSeparator();
  menu.Append(ID_TEX_COPY_PATH, _("Copy path"));
  menu.Append(ID_TEX_COPY_ID, _("Copy FileDataID"));
  menu.Enable(ID_TEX_COPY_PATH, !entry.unnamed && !entry.path.isEmpty());
  menu.Enable(ID_TEX_COPY_ID, entry.fileDataId > 0);
  // A search waiting to run would rebuild the tree, row included, while the menu is open.
  const bool searchPending = m_searchTimer.IsRunning();
  m_searchTimer.Stop();
  const int id = GetPopupMenuSelectionFromUser(menu);
  if (searchPending)
    m_searchTimer.StartOnce(300);
  if (id == wxID_NONE)
    return;
  // The texture acted on is the one selected: selected first (decoded now, for an export).
  fileTree->SelectItem(item);
  modelviewer->ShowTexture(entry, lookup);
  TextureView * view = modelviewer->textureView;
  view->showNow(entry, lookup);
  switch (id)
  {
    case ID_TEX_PNG: view->exportPngInteractive(); break;
    case ID_TEX_BLP: view->exportBlpInteractive(); break;
    case ID_TEX_COPY_PATH: view->copyPath(); break;
    case ID_TEX_COPY_ID: view->copyFileDataId(); break;
    default: break;
  }
}

void FileControl::TexturesClientLoadStarting()
{
  // The kept model and building trees, and the buildings listed, point into the old client's files.
  forgetTrees();
  m_textures->clientLoadStarting();
  if (ShowsTextures())
    m_textures->clear();
  m_texturesLoadWatch.Start(250);
}

void FileControl::TexturesClientLoaded()
{
  if (UnityAssetAccess::isClientLoading())
    return;
  m_texturesLoadWatch.Stop();
  if (ShowsTextures())
    Init();
  // The load is over, whether it succeeded or not: the texture view leaves its "loading" state, and the
  // viewport, the command bar and the menus follow what is loaded now (a failed load queues no call of
  // its own, and a finished one makes its call while it is still under way).
  if (modelviewer && modelviewer->textureView)
    modelviewer->textureView->clientLoaded();
  UpdateInterface();
}

void FileControl::OnTreeActivated(wxTreeEvent &event)
{
  // Enter or a double-click on a texture row selects it (a click on the row that is already selected is
  // no selection change; the view keeps a texture already selected as it is); on a building row, it loads it
  // (the building already on screen stays as it is).
  if (m_mode == ViewerMode::Textures)
    SelectTextureRow(event.GetItem());
  else if (m_mode == ViewerMode::Buildings)
    SelectBuildingRow(event.GetItem());
  event.Skip();
}

void FileControl::TextureLookupResolved(int fileDataId, const QString & indexName)
{
  m_textures->lookupResolved(fileDataId, indexName);
}
