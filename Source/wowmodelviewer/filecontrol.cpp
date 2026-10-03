#include "logger/Logger.h"

#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/srchctrl.h>

#include <functional>

#include <QDirIterator>
#include <QElapsedTimer>
#include <QImage>
#include <QRegularExpression>

#include "CASCFile.h"
#include "Game.h"
#include "globalvars.h"
#include "logger/Logger.h"
#include "modelviewer.h"
#include "RaceInfos.h"
#include "TextureBrowse.h"
#include "TextureView.h"
#include "UiStyle.h"
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
// with no choice of its own: in Models mode what the viewport draws -- models (*.m2) and world models
// (root *.wmo) -- and in Textures mode the client's textures (TextureBrowse). Other kinds of file are not
// listed: the viewer has nothing to show them with.
static QString content;

// A WMO group or LOD file by its name: "_NNN_" anywhere in the path, or a name ending "_NNN.wmo" or
// "lodN.wmo" (the reference implementation's group/LOD pattern, (_\d\d\d_)|(_\d\d\d\.wmo$)|(lod\d\.wmo$)
// on the lower-cased path; written out, it costs a fraction of the regular expression on the 86,000 .wmo
// names of a 12.1 client). A name rule only: the few roots named like a group (11xt_rockbridge_003.wmo,
// see SelectWMOFile) are hidden with the groups, as Browse's WMO list always hid them.
static bool isWmoGroupOrLod(const QString & path)
{
  const int n = path.size();
  auto digit = [&path](int i) { return path[i] >= QLatin1Char('0') && path[i] <= QLatin1Char('9'); };
  auto is = [&path](int i, char c) { return path[i].toLower() == QLatin1Char(c); };
  if (n >= 8 && path.endsWith(QLatin1String(".wmo"), Qt::CaseInsensitive))
  {
    if (is(n - 8, '_') && digit(n - 7) && digit(n - 6) && digit(n - 5))
      return true;
    if (is(n - 8, 'l') && is(n - 7, 'o') && is(n - 6, 'd') && digit(n - 5))
      return true;
  }
  for (int i = 0; i + 4 < n; ++i)
    if (path[i] == QLatin1Char('_') && digit(i + 1) && digit(i + 2) && digit(i + 3) && path[i + 4] == QLatin1Char('_'))
      return true;
  return false;
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
  m_treeRoot = NULL;
  fileTree = NULL;
  m_searchTimer.SetOwner(this, ID_FILELIST_SEARCHTIMER);

  if (Create(parent, id, wxDefaultPosition, wxSize(260,700), 0, wxT("ModelControlFrame")) == false) {
    LOG_ERROR << "Failed to create a window for our FileControl!";
    return;
  }

  try {
    const int sp = FromDIP(UiStyle::S);

    // The search box, the tree, and one line under the tree: the minimum length while typing, how many
    // files a search found, or why there is nothing to list. The search is the viewer mode's (its hint
    // says which: UpdateSearchHint).
    txtContent = new wxSearchCtrl(this, ID_FILELIST_CONTENT, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                  wxTE_PROCESS_ENTER);
    txtContent->ShowSearchButton(true);
    txtContent->ShowCancelButton(true);
    searchStatus = UiStyle::secondaryLabel(this, wxEmptyString);

    fileTree = new wxTreeCtrl(this, ID_FILELIST, wxDefaultPosition, wxDefaultSize, wxTR_HIDE_ROOT|wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT|wxTR_FULL_ROW_HIGHLIGHT|wxTR_NO_LINES);
    m_textures = new TextureBrowse(fileTree);
    m_texturesLoadWatch.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { TexturesClientLoaded(); });
    // An arrow key held down in the texture tree: the rows it passes wait to be decoded until it is let
    // go (TextureView::setKeysRepeating). Focus leaving the tree lets go too: the key-up goes elsewhere.
    fileTree->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent & e) { KeysRepeating(e.IsAutoRepeat()); e.Skip(); });
    fileTree->Bind(wxEVT_KEY_UP, [this](wxKeyEvent & e) { KeysRepeating(false); e.Skip(); });
    fileTree->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent & e) { KeysRepeating(false); e.Skip(); });

    wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
    top->Add(txtContent, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
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
  txtContent->Destroy();
}

void FileControl::SetSearchStatus(const wxString & text)
{
  if (searchStatus->GetLabel() == text)
    return;
  searchStatus->SetLabel(text);
  searchStatus->Wrap(wxMax(FromDIP(100), GetClientSize().x - 2 * FromDIP(UiStyle::S)));
  searchStatus->Show(!text.IsEmpty());
  Layout();
}

void FileControl::UpdateSearchHint()
{
  if (m_showsTextures)
  {
    txtContent->SetDescriptiveText(_("Search textures"));
    txtContent->SetToolTip(_("Textures by name or path -- or FileDataID, on a client that has them -- as you type "
                             "from 3 characters; press Enter to search a shorter term, or to open the texture with "
                             "that FileDataID"));
  }
  else
  {
    txtContent->SetDescriptiveText(_("Search models"));
    txtContent->SetToolTip(_("Models and world models by file name, as you type from 3 characters; press Enter "
                             "to search a shorter term"));
  }
}

void FileControl::Init(ModelViewer* mv)
{
  if (modelviewer == NULL)
    modelviewer = mv;

  // A search is running now, so cancel any pending debounced one (Enter/Clear/timer all
  // funnel through here -- this stops a queued timer from re-searching the same text).
  m_searchTimer.Stop();

  if (m_showsTextures)
  {
    InitTextures();
    return;
  }

  // No client yet: there is no file index to list.
  if (!core::Game::instance().initDone())
  {
    fileTree->DeleteAllItems();
    m_modelsApplied.Clear();
    SetSearchStatus(_("Load a World of Warcraft client to browse its files."));
    return;
  }
  m_modelsApplied = txtContent->GetValue();
  content = QString(QString::fromWCharArray(txtContent->GetValue().c_str()).toLower().trimmed());
  if (reuseModelTree())
    return;
  m_modelTreeKept = false;

  LOG_INFO << "Initializing File Controls - Start";

  // The models and world models whose name holds the search text, in one pass over the file index: the
  // test GameFolder::getFilteredFiles makes for one extension -- the ending first, then the search (none
  // to make while browsing: every name passes it) -- for both. WMO group and LOD files ("<name>_000.wmo",
  // "..._000_lod1.wmo", etc.) are not standalone world models -- the root references its groups -- so
  // only roots are listed.
  std::set<GameFile *> files;
  const QRegularExpression m2Search("^.*" + content + ".*\\.m2"), wmoSearch("^.*" + content + ".*\\.wmo");
  if (!m2Search.isValid())
    LOG_ERROR << m2Search.errorString();
  else
    for (auto it = GAMEDIRECTORY.begin(); it != GAMEDIRECTORY.end(); ++it)
    {
      const QString name = (*it)->name();
      const bool model = name.endsWith(QLatin1String(".m2")), worldModel = !model && name.endsWith(QLatin1String(".wmo"));
      if (!model && !worldModel)
        continue;
      if (!content.isEmpty() && !name.contains(model ? m2Search : wmoSearch))
        continue;
      if (worldModel && isWmoGroupOrLod((*it)->fullname()))
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

  // Build a fresh hierarchy and keep it on the control (the previous one is left to
  // leak -- the Component ref-counting underflows on unref, so the tree always has;
  // this matches the prior behaviour while letting branches be filled in on expand).
  m_treeRoot = new TreeStackItem();
  TreeStackItem & root = *m_treeRoot;
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
  fileTree->DeleteAllItems();
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
  m_treeRootValid = true;
  m_treeRootContent = content;
  m_treeRootStatus = searchStatus->GetLabel();

  LOG_INFO << "Initializing File Controls - END";
}

// Lazy tree fill-in: when a collapsed branch is expanded, add its direct children
// to the wxTreeCtrl (idempotent -- appendChildren no-ops once a node is loaded).
void FileControl::OnTreeItemExpanding(wxTreeEvent &event)
{
  const wxTreeItemId item = event.GetItem();
  if (!item.IsOk())
    return;
  if (m_showsTextures)
  {
    m_textures->expanding(item);
    return;
  }
  FileTreeData * data = (FileTreeData *)fileTree->GetItemData(item);
  if (data && data->node)
    data->node->appendChildren(fileTree);
}

// copy from ModelOpened::Export
void FileControl::Export(wxString val, int select)
{
  if (val.IsEmpty())
    return;

  GameFile * f = GAMEDIRECTORY.getFile(QString::fromWCharArray(val.c_str()));
  if(!f)
  {
    LOG_ERROR << "Could not extract" << QString::fromWCharArray(val.c_str());
    return;
  }

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
  wxString val(data->file->fullname().toStdWString());

  if (evt.GetId() == ID_FILELIST_SAVE)
    Export(val, 1);
}

void FileControl::OnTreeMenu(wxTreeEvent &event)
{
  wxTreeItemId item = event.GetItem();

  if (!item.IsOk() || !modelviewer->canvas) // make sure that a valid Tree Item was actually selected.
    return;

  if (m_showsTextures)
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
  if (!modelviewer->isModel && !modelviewer->isWMO && !modelviewer->isADT)
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
    if (modelviewer->isChar) {
      modelviewer->charControl->charAtt = NULL;
    }
    //wxDELETE(modelviewer->canvas->model); // may memory leak
    modelviewer->canvas->setModel(NULL);
  } else if (modelviewer->isADT) {
    wxDELETE(modelviewer->canvas->adt);
    modelviewer->canvas->adt = NULL;
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
  modelviewer->isADT = false;
}

void FileControl::UpdateInterface()
{
  // Disable whatever formats can't be export yet.

  // Don't run if there aren't any models loaded!
  if (modelviewer == NULL)
    return;

  // You MUST put true in one if the other is false! Otherwise, if they open the other model type and go back,
  // your function will still be disabled!!
  // A model kept loaded behind a texture is not on screen: its character commands wait until it is.
  if (modelviewer->isModel == true && !modelviewer->isTextureMode()){
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
  }else if (modelviewer->isADT == true){
    // If it's an ADT file...
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
  // Randomise and eye glow (LoadModel enables them for a character) wait in Textures mode too.
  if (modelviewer->isTextureMode())
  {
    modelviewer->charMenu->Enable(ID_CHAR_RANDOMISE, false);
    modelviewer->charMenu->Enable(ID_CHAREYEGLOW, false);
  }
  else if (modelviewer->isModel && modelviewer->isChar)
  {
    modelviewer->charMenu->Enable(ID_CHAR_RANDOMISE, true);
    modelviewer->charMenu->Enable(ID_CHAREYEGLOW, true);
  }

  // The Model panel follows whatever was just opened (a model, a WMO, a map tile, nothing).
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

  if (m_showsTextures)
  {
    SelectTextureRow(item);
    return;
  }

  FileTreeData *data = (FileTreeData*)fileTree->GetItemData(item);

  // make sure the data (file name) is valid
  if (!data || !data->file){
    return; // isn't valid, exit.
  }

  CurrentItem = item;

  // A world model row loads the world model; a model row the model (a race-browser leaf names the race and
  // sex it stands for; an ordinary file row does not).
  if (isWorldModel(data->file))
    SelectWMOFile(data->file);
  else
    SelectModelFile(data->file,
                    data->node ? data->node->raceID : -1,
                    data->node ? data->node->sexID : -1);
}

bool FileControl::isWorldModel(GameFile * file)
{
  return file && file->fullname().endsWith(QLatin1String(".wmo"), Qt::CaseInsensitive);
}

bool FileControl::isLoaded(GameFile * file) const
{
  if (!file || !modelviewer || !modelviewer->canvas)
    return false;
  if (isWorldModel(file))
    return modelviewer->isWMO && modelviewer->canvas->wmo &&
           modelviewer->canvas->wmo->itemName().compare(file->fullname(), Qt::CaseInsensitive) == 0;
  const WoWModel * model = modelviewer->canvas->model();
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
void FileControl::FollowViewerMode(bool textures)
{
  if (textures == m_showsTextures)
    return;
  // The tree left is kept for its return: the texture tree's open folders, top row and picked row; the
  // model tree itself (keepModelTree).
  if (m_showsTextures)
    m_textures->rememberOpenFolders();
  else
    keepModelTree();
  // Each side's search is what its tree shows, not text typed and not run: that one could be a
  // 1-character search over every model, which takes seconds.
  (m_showsTextures ? m_texturesSearch : m_modelsSearch) = m_showsTextures ? m_texturesApplied : m_modelsApplied;
  txtContent->ChangeValue(textures ? m_texturesSearch : m_modelsSearch);
  m_showsTextures = textures;
  UpdateSearchHint();
  Init();
}

void FileControl::PickedRowFollowsLoad()
{
  // A model loaded from a menu, a saved character or an import has no row here, and the row picked before
  // would claim it -- and a click on that row would do nothing, being picked already.
  if (m_showsTextures || !fileTree)
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
    modelviewer->textureView->setKeysRepeating(repeating && m_showsTextures);
}

void FileControl::keepModelTree()
{
  // Only the tree built for this search, and only while that hierarchy is the client's.
  m_modelTreeKept = false;
  m_modelOpenNodes.clear();
  m_modelTopNode = nullptr;
  m_modelPickedNode = nullptr;
  if (!m_treeRoot || !m_treeRootValid || !fileTree->GetRootItem().IsOk())
    return;
  std::function<void(wxTreeItemId)> collect = [&](wxTreeItemId parent) {
    wxTreeItemIdValue cookie;
    for (wxTreeItemId c = fileTree->GetFirstChild(parent, cookie); c.IsOk(); c = fileTree->GetNextChild(parent, cookie))
    {
      FileTreeData * data = dynamic_cast<FileTreeData *>(fileTree->GetItemData(c));
      if (!data || !data->node || !fileTree->IsExpanded(c))
        continue;
      m_modelOpenNodes.insert(data->node);
      collect(c);
    }
  };
  collect(fileTree->GetRootItem());
  const wxTreeItemId top = fileTree->GetFirstVisibleItem();
  FileTreeData * topData = top.IsOk() ? dynamic_cast<FileTreeData *>(fileTree->GetItemData(top)) : nullptr;
  m_modelTopNode = topData ? topData->node : nullptr;
  const wxTreeItemId picked = fileTree->GetSelection();
  FileTreeData * pickedData = picked.IsOk() ? dynamic_cast<FileTreeData *>(fileTree->GetItemData(picked)) : nullptr;
  m_modelPickedNode = pickedData ? pickedData->node : nullptr;
  m_modelTreeKept = true;
}

void FileControl::reopenModelRows(wxTreeItemId parent, wxTreeItemId & top, wxTreeItemId & picked)
{
  wxTreeItemIdValue cookie;
  for (wxTreeItemId c = fileTree->GetFirstChild(parent, cookie); c.IsOk(); c = fileTree->GetNextChild(parent, cookie))
  {
    FileTreeData * data = dynamic_cast<FileTreeData *>(fileTree->GetItemData(c));
    if (!data || !data->node)
      continue;
    if (data->node == m_modelTopNode)
      top = c;
    if (data->node == m_modelPickedNode)
      picked = c;
    if (m_modelOpenNodes.count(data->node))
    {
      fileTree->Expand(c);   // filled by OnTreeItemExpanding
      reopenModelRows(c, top, picked);
    }
  }
}

bool FileControl::reuseModelTree()
{
  if (!m_modelTreeKept || !m_treeRoot || !m_treeRootValid || m_treeRootContent != content)
    return false;
  m_modelTreeKept = false;
  QElapsedTimer timer;
  timer.start();
  TreeStackItem & root = *m_treeRoot;
  root.resetLoaded();
  fileTree->Freeze();
  fileTree->DeleteAllItems();
  root.id = fileTree->AddRoot(wxT("Root"));
  wxTreeItemId top, picked;
  // (A search's rows are all made at once; the folders open again are the ones that were open.)
  if (content.isEmpty())
    root.appendChildren(fileTree);
  else
    root.createTreeItems(fileTree);
  reopenModelRows(root.id, top, picked);
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
  SetSearchStatus(m_treeRootStatus);
  LOG_INFO << "Initializing File Controls - the tree kept, rows again in" << timer.elapsed() << "ms,"
           << (int)m_modelOpenNodes.size() << "folders opened again";
  return true;
}

// ---------------------------------------------------------------------------------------- Textures mode
// The client's textures from the texture catalogue (TextureBrowse), in this tree, with this search
// box: see TextureBrowse.h for why not the hierarchy the models' tree is built from.

bool FileControl::ShowsTextures() const
{
  return m_showsTextures;
}

void FileControl::InitTextures()
{
  const QString term = QString::fromWCharArray(txtContent->GetValue().c_str()).trimmed();
  if (!m_textures->ensureCatalog())
  {
    m_textures->clear();
    m_texturesApplied.Clear();
    SetSearchStatus(UnityAssetAccess::isClientLoading() ? _("Loading the game client...")
                                                         : _("Load a World of Warcraft client to browse its files."));
    return;
  }
  const bool fromSearch = m_textures->showsSearch();
  m_texturesApplied = txtContent->GetValue();
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
  // The kept model tree points into the old client's files.
  m_modelTreeKept = false;
  m_modelOpenNodes.clear();
  m_modelTopNode = nullptr;
  m_modelPickedNode = nullptr;
  m_treeRootValid = false;
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
  // no selection change; the view keeps a texture already selected as it is).
  if (m_showsTextures)
    SelectTextureRow(event.GetItem());
  event.Skip();
}

void FileControl::TextureLookupResolved(int fileDataId, const QString & indexName)
{
  m_textures->lookupResolved(fileDataId, indexName);
}
