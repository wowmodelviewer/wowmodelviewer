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
  EVT_CHOICE(ID_FILELIST_FILTER, FileControl::OnChoice)
  EVT_TREE_ITEM_MENU(ID_FILELIST, FileControl::OnTreeMenu)
  EVT_TREE_ITEM_ACTIVATED(ID_FILELIST, FileControl::OnTreeActivated)
END_EVENT_TABLE()

// One entry per filter in the choice list below (chos, filterStrings), in the same order: the
// selected index IS the filter mode. OGG and SKIN were missing, which put every later entry one out
// of step -- "MP3s" ran the image branch and "Images (*.blp)" only cleared the model.
// Textures is last: choosing it is the texture viewer (ModelViewer::SetViewerMode), so stepping through
// the file categories with the arrow keys must not pass it on the way.
enum FilterModes {
  FILE_FILTER_MODEL=0,
  FILE_FILTER_WMO,
  FILE_FILTER_ADT,
  FILE_FILTER_WAV,
  FILE_FILTER_OGG,
  FILE_FILTER_MP3,
  FILE_FILTER_BLS,
  FILE_FILTER_DBC,
  FILE_FILTER_DB2,
  FILE_FILTER_LUA,
  FILE_FILTER_XML,
  FILE_FILTER_SKIN,
  FILE_FILTER_TEXTURE,

  FILE_FILTER_MAX
};

/*
All suffixs in MPQ:
.adt .anim .blob .BLP .bls .bundle .cfg .css .db .dbc .DELETE .dll .error .exe
.gif .html .icns .ini .jpg .js .log .lua .M2 .mp3 .mpq .nib .not .pdf .plist .png
.rsrc .sbt .SIG .skin .test .tiff .toc .trs .TTF .txt .url .uvw .wav .wdl .wdt
.wfx .what .wmo .wtf .xib .xml .xsd .zmp 
*/
static QString content;
static QString filterString;
static QString filterStrings[] = {"m2", "wmo", "adt", "wav", "ogg", "mp3",
  "bls", "dbc", "db2", "lua", "xml", "skin", "blp"};
static wxString chos[] = {wxT("Models (*.m2)"), wxT("WMOs (*.wmo)"), wxT("ADTs (*.adt)"), wxT("WAVs (*.wav)"), wxT("OGGs (*.ogg)"), wxT("MP3s (*.mp3)"),
  wxT("Shaders (*.bls)"), wxT("DBCs (*.dbc)"), wxT("DB2s (*.db2)"), wxT("LUAs (*.lua)"), wxT("XMLs (*.xml)"), wxT("SKINs (*.skin)"), wxT("Textures (*.blp)")};

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
  filterMode = FILE_FILTER_MODEL;
  m_treeRoot = NULL;
  fileTree = NULL;
  m_searchTimer.SetOwner(this, ID_FILELIST_SEARCHTIMER);

  if (Create(parent, id, wxDefaultPosition, wxSize(260,700), 0, wxT("ModelControlFrame")) == false) {
    LOG_ERROR << "Failed to create a window for our FileControl!";
    return;
  }

  try {
    const int xs = FromDIP(UiStyle::XS);
    const int sp = FromDIP(UiStyle::S);

    // Search: the box, then one line under it that explains the minimum length while typing and
    // reports how many files matched afterwards.
    wxStaticText * searchLabel = new wxStaticText(this, wxID_ANY, _("Search"));
    txtContent = new wxSearchCtrl(this, ID_FILELIST_CONTENT, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                  wxTE_PROCESS_ENTER);
    txtContent->ShowSearchButton(true);
    txtContent->ShowCancelButton(true);
    txtContent->SetDescriptiveText(_("Name or path"));
    txtContent->SetToolTip(_("Searches as you type from 3 characters; press Enter to search a shorter term"));
    searchStatus = UiStyle::secondaryLabel(this, wxEmptyString);

    wxStaticText * filterLabel = new wxStaticText(this, wxID_ANY, _("Show"));
    choFilter = new wxChoice(this, ID_FILELIST_FILTER, wxDefaultPosition, wxDefaultSize, WXSIZEOF(chos), chos);
    choFilter->SetSelection(filterMode);

    fileTree = new wxTreeCtrl(this, ID_FILELIST, wxDefaultPosition, wxDefaultSize, wxTR_HIDE_ROOT|wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT|wxTR_FULL_ROW_HIGHLIGHT|wxTR_NO_LINES);
    m_textures = new TextureBrowse(fileTree);
    m_texturesLoadWatch.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { TexturesClientLoaded(); });
    // An arrow key held down in the texture tree: the rows it passes wait to be decoded until it is let
    // go (TextureView::setKeysRepeating). Focus leaving the tree lets go too: the key-up goes elsewhere.
    fileTree->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent & e) { KeysRepeating(e.IsAutoRepeat()); e.Skip(); });
    fileTree->Bind(wxEVT_KEY_UP, [this](wxKeyEvent & e) { KeysRepeating(false); e.Skip(); });
    fileTree->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent & e) { KeysRepeating(false); e.Skip(); });

    wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
    top->Add(searchLabel, 0, wxLEFT | wxRIGHT | wxTOP, sp);
    (void)xs;
    top->Add(txtContent, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
    top->Add(searchStatus, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
    wxBoxSizer * filterRow = new wxBoxSizer(wxHORIZONTAL);
    filterRow->Add(filterLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, sp);
    filterRow->Add(choFilter, 1, wxALIGN_CENTER_VERTICAL);
    top->Add(filterRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, sp);
    top->Add(fileTree, 1, wxEXPAND | wxTOP, sp);
    SetSizer(top);

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
  choFilter->Destroy();
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

bool filterSearch(QString s)
{
  if(s.length() < 4)
    return false;

  // filter suffix
  if (!filterString.isEmpty() && !s.toLower().endsWith(filterString))
    return false;

  // filter text input
  if (!content.isEmpty() && s.toLower().indexOf(content) == -1)
    return false;

  return true;
}

void FileControl::Init(ModelViewer* mv)
{
  if (modelviewer == NULL)
    modelviewer = mv;

  // A search is running now, so cancel any pending debounced one (Enter/Clear/timer all
  // funnel through here -- this stops a queued timer from re-searching the same text).
  m_searchTimer.Stop();

  if (filterMode == FILE_FILTER_TEXTURE)
  {
    InitTextures();
    return;
  }

  // No client yet: there is no file index to list (choosing a category in Show before loading one).
  if (!core::Game::instance().initDone())
  {
    fileTree->DeleteAllItems();
    m_filesApplied.Clear();
    SetSearchStatus(_("Load a World of Warcraft client to browse its files."));
    return;
  }
  m_filesApplied = txtContent->GetValue();
  content = QString(QString::fromWCharArray(txtContent->GetValue().c_str()).toLower().trimmed());
  if (reuseModelTree())
    return;
  m_modelTreeKept = false;

  LOG_INFO << "Initializing File Controls - Start";

  // Gets the list of files that meet the filter criteria
  // and puts them into an array to be processed into our file tree
  content = QString(QString::fromWCharArray(txtContent->GetValue().c_str()).toLower().trimmed());
  filterString = "^.*"+ content +".*\\." + filterStrings[filterMode];
  std::set<GameFile *> files;
  GAMEDIRECTORY.getFilteredFiles(files, filterString);

  LOG_INFO << "Initializing File Controls - Filtering done - files found" << files.size();

  // When listing models, the raw character/ folder is replaced by the curated
  // "Characters" race browser built below (Playable / NPC), so skip the raw
  // character/ entries here to avoid showing both. Other filters (textures, etc.)
  // keep the character/ folder since there's no race browser for them.
  // Only substitute the race browser when NOT searching: during a search the
  // curated node ignores the query, so the raw character/ matches must remain
  // visible or character searches would return nothing.
  const bool buildRaceTree = (filterStrings[filterMode] == "m2") && content.isEmpty();

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

    // Hide WMO group + LOD files ("<name>_000.wmo", "..._000_lod1.wmo", etc.). They are not
    // standalone WMOs -- the root WMO references its groups internally -- so only roots are
    // listed. Pattern mirrors the reference implementation's WMO group/LOD filter.
    if (filterMode == FILE_FILTER_WMO)
    {
      static const QRegularExpression wmoGroupLod("(_\\d\\d\\d_)|(_\\d\\d\\d\\.wmo$)|(lod\\d\\.wmo$)");
      if (wmoGroupLod.match((*it)->fullname().toLower()).hasMatch())
        continue;
    }

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
  m_treeRootFilter = filterMode;
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
  if (filterMode == FILE_FILTER_TEXTURE)
  {
    m_textures->expanding(item);
    return;
  }
  FileTreeData * data = (FileTreeData *)fileTree->GetItemData(item);
  if (data && data->node)
    data->node->appendChildren(fileTree);
}

void FileControl::OnChoice(wxCommandEvent &event)
{
  int id = event.GetId();
  if (id == ID_FILELIST_FILTER) {
    int curSelection = choFilter->GetCurrentSelection();
    if (curSelection >= 0 && curSelection != filterMode)
    {
      // Into or out of Textures: that is the viewer mode, which brings Browse along.
      const bool toTextures = curSelection == FILE_FILTER_TEXTURE;
      if (modelviewer && toTextures != (filterMode == FILE_FILTER_TEXTURE))
      {
        if (!toTextures)
          m_lastFilesCategory = curSelection;
        modelviewer->SetViewerMode(toTextures ? ModelViewer::ViewerMode::Textures : ModelViewer::ViewerMode::Models);
      }
      else
        setCategory(curSelection);
    }
  }
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

  if (filterMode == FILE_FILTER_TEXTURE)
  {
    ShowTextureMenu(item);
    return;
  }

  void *data = reinterpret_cast<void *>(fileTree->GetItemData(item));
  FileTreeData *tdata = (FileTreeData*)data;

  // make sure the data (file name) is valid: folder rows carry no file
  if (!data || !tdata->file)
    return; // isn't valid, exit.

  // Make a menu to export it (a texture is looked at in the viewport: Show: Textures)
  wxMenu infoMenu;
  infoMenu.SetClientData( data );
  infoMenu.Append(ID_FILELIST_SAVE, wxT("&Save..."), wxT("Save this object"));
  // TODO: if is music, a Play option

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

  // make sure that a valid Tree Item was actually selected.
  if (!item.IsOk() || !modelviewer->canvas){
    return;
  }

  if (filterMode == FILE_FILTER_TEXTURE)
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

  if (filterMode == FILE_FILTER_MODEL) {
    // A race-browser leaf names the race and sex it stands for; an ordinary file row does not.
    SelectModelFile(data->file,
                    data->node ? data->node->raceID : -1,
                    data->node ? data->node->sexID : -1);
  } else if (filterMode == FILE_FILTER_WMO) {
    SelectWMOFile(data->file);
  } else if (filterMode == FILE_FILTER_ADT) {
    ClearCanvas();

    modelviewer->isADT = true;
    wxString rootfn(data->file->fullname().toStdWString());
    modelviewer->canvas->LoadADT(rootfn);

    UpdateInterface();
  } else {
    ClearCanvas();

    UpdateInterface();
  }
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
  setCategory(textures ? FILE_FILTER_TEXTURE : m_lastFilesCategory);
}

void FileControl::ShowModels()
{
  // What the viewport shows -- models, world models, map tiles -- stays; anything else becomes the models.
  auto shown = [](int mode) { return mode == FILE_FILTER_MODEL || mode == FILE_FILTER_WMO || mode == FILE_FILTER_ADT; };
  if (filterMode == FILE_FILTER_TEXTURE)
  {
    if (!shown(m_lastFilesCategory))
      m_lastFilesCategory = FILE_FILTER_MODEL;   // where leaving Textures goes (FollowViewerMode)
  }
  else if (!shown(filterMode))
    setCategory(FILE_FILTER_MODEL);
}

void FileControl::KeysRepeating(bool repeating)
{
  if (modelviewer && modelviewer->textureView)
    modelviewer->textureView->setKeysRepeating(repeating && filterMode == FILE_FILTER_TEXTURE);
}

void FileControl::setCategory(int mode)
{
  if (mode == filterMode)
    return;
  const bool fromTextures = filterMode == FILE_FILTER_TEXTURE, toTextures = mode == FILE_FILTER_TEXTURE;
  if (fromTextures)
    m_textures->rememberOpenFolders();
  else
  {
    m_lastFilesCategory = filterMode;
    if (toTextures)
      keepModelTree();
  }
  if (fromTextures != toTextures)
  {
    // What its tree shows, not text typed and not run: that one could be a 1-character search over
    // every model, which takes seconds.
    (fromTextures ? m_texturesSearch : m_filesSearch) = fromTextures ? m_texturesApplied : m_filesApplied;
    txtContent->ChangeValue(toTextures ? m_texturesSearch : m_filesSearch);
  }
  filterMode = mode;
  choFilter->SetSelection(filterMode);
  Init();
}

void FileControl::keepModelTree()
{
  // Only the tree built for this category and search, and only while that hierarchy is the client's.
  m_modelTreeKept = false;
  m_modelOpenNodes.clear();
  m_modelTopNode = nullptr;
  if (!m_treeRoot || m_treeRootFilter != filterMode || !fileTree->GetRootItem().IsOk())
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
  m_modelTreeKept = true;
}

void FileControl::reopenModelRows(wxTreeItemId parent, wxTreeItemId & top)
{
  wxTreeItemIdValue cookie;
  for (wxTreeItemId c = fileTree->GetFirstChild(parent, cookie); c.IsOk(); c = fileTree->GetNextChild(parent, cookie))
  {
    FileTreeData * data = dynamic_cast<FileTreeData *>(fileTree->GetItemData(c));
    if (!data || !data->node)
      continue;
    if (data->node == m_modelTopNode)
      top = c;
    if (m_modelOpenNodes.count(data->node))
    {
      fileTree->Expand(c);   // filled by OnTreeItemExpanding
      reopenModelRows(c, top);
    }
  }
}

bool FileControl::reuseModelTree()
{
  if (!m_modelTreeKept || !m_treeRoot || m_treeRootFilter != filterMode || m_treeRootContent != content)
    return false;
  m_modelTreeKept = false;
  QElapsedTimer timer;
  timer.start();
  TreeStackItem & root = *m_treeRoot;
  root.resetLoaded();
  fileTree->Freeze();
  fileTree->DeleteAllItems();
  root.id = fileTree->AddRoot(wxT("Root"));
  wxTreeItemId top;
  if (content.isEmpty())
  {
    root.appendChildren(fileTree);
    reopenModelRows(root.id, top);
  }
  else
  {
    root.createTreeItems(fileTree);
    fileTree->ExpandAll();
  }
  if (top.IsOk())
    fileTree->ScrollTo(top);
  fileTree->Thaw();
  SetSearchStatus(m_treeRootStatus);
  LOG_INFO << "Initializing File Controls - the tree kept, rows again in" << timer.elapsed() << "ms,"
           << (int)m_modelOpenNodes.size() << "folders opened again";
  return true;
}

// ----------------------------------------------------------------------------------- Show: Textures
// The client's textures from the texture catalogue (TextureBrowse), in this tree, with this search
// box: see TextureBrowse.h for why not the hierarchy the other categories build.

bool FileControl::ShowsTextures() const
{
  return filterMode == FILE_FILTER_TEXTURE;
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
  m_treeRootFilter = -1;
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
  if (filterMode == FILE_FILTER_TEXTURE)
    SelectTextureRow(event.GetItem());
  event.Skip();
}

void FileControl::TextureLookupResolved(int fileDataId, const QString & indexName)
{
  m_textures->lookupResolved(fileDataId, indexName);
}
