#ifndef FILECONTROL_H
#define FILECONTROL_H

#include "FileTreeItem.h"
#include "ViewerMode.h"

class ModelViewer;

#include <wx/string.h>
#include <wx/treectrl.h> // wxTreeItemId
#include <wx/timer.h>    // wxTimer for debounced as-you-type search

#include <set>
#include <utility>
#include <vector>

class wxSearchCtrl;
class wxStaticText;
class TextureBrowse;

#include "metaclasses/Container.h"

class TreeStackItem; // defined below

class FileTreeData:public wxTreeItemData
{
public:
  GameFile * file;
  TreeStackItem * node; // owning hierarchy node, for lazily filling in children on expand
  // A Buildings search's "Look up FileDataID N" row (no file, no node): the number to look up when picked.
  int lookupFileDataId = 0;
  FileTreeData(GameFile * f, TreeStackItem * n = 0): file(f), node(n) {}
};

// One node of the file tree. The whole path hierarchy is built up front (cheap),
// but its rows are only added to the wxTreeCtrl when a branch is expanded --
// building all ~130k rows eagerly took ~9s and dominated startup and every search.
class TreeStackItem : public Container<TreeStackItem>
{
  public:
    wxTreeItemId id;
    GameFile * file;
    bool loaded;   // have this node's children been added to the wxTreeCtrl yet?
    // Set on the leaves of the "Characters" race browser, where the row names a race and sex
    // rather than just a file: races sharing a model file (Mag'har Orc on the Orc model) are
    // told apart only by this. -1 on every other row.
    int raceID;
    int sexID;

    TreeStackItem() : file(0), loaded(false), raceID(-1), sexID(-1) {}

    // The rows went (the tree showed the textures): nothing below is in the tree any more.
    void resetLoaded()
    {
      loaded = false;
      for (std::map<QString, TreeStackItem *>::iterator it = m_childrenMap.begin(); it != m_childrenMap.end(); ++it)
        it->second->resetLoaded();
    }

    bool hasChildren() const { return !m_childrenMap.empty(); }

    TreeStackItem * getChildByName(QString name)
    {
      std::map<QString, TreeStackItem *>::iterator it = m_childrenMap.find(name);
      if(it != m_childrenMap.end())
        return it->second;
      return 0;
    }

    void onChildAdded(TreeStackItem * child)
    {
      m_childrenMap[child->name()] = child;
      // The node owns its children: Container's destructor drops this reference, so deleting a node deletes the
      // subtree below it. (addChild takes none; without it the drop wrapped the count and nothing was ever freed.)
      child->ref();
    }

    // LAZY: add only this node's direct children to the tree, marking branches as
    // having children (the expand arrow) without recursing. Idempotent.
    void appendChildren(wxTreeCtrl * tree)
    {
      if (loaded)
        return;
      loaded = true;
      for(std::map<QString, TreeStackItem *>::iterator it = m_childrenMap.begin(); it != m_childrenMap.end(); ++it)
      {
        TreeStackItem * c = it->second;
        c->id = tree->AppendItem(id, c->name().toStdWString(), -1, -1, new FileTreeData(c->file, c));
        if (c->hasChildren())
          tree->SetItemHasChildren(c->id, true);
      }
    }

    // EAGER: add the whole subtree at once. Used for search results, which are small.
    void createTreeItems(wxTreeCtrl * tree)
    {
      loaded = true;
      for(std::map<QString, TreeStackItem *>::iterator it = m_childrenMap.begin(); it != m_childrenMap.end(); ++it)
      {
        TreeStackItem * c = it->second;
        c->id = tree->AppendItem(id, c->name().toStdWString(), -1, -1, new FileTreeData(c->file, c));
        c->createTreeItems(tree);
      }
    }

  private:
    std::map<QString, TreeStackItem *> m_childrenMap;
};

class FileControl: public wxWindow
{
  DECLARE_CLASS(FileControl)
  DECLARE_EVENT_TABLE()

public:
  // Constructor + Deconstructor
  FileControl(wxWindow* parent, wxWindowID id);
  ~FileControl();

  void Init(ModelViewer* mv=NULL);
  void OnTreeSelect(wxTreeEvent &event);
  void OnTreeItemExpanding(wxTreeEvent &event);
  void OnButton(wxCommandEvent &event);
  void OnSearchText(wxCommandEvent &event);  // restarts the debounce timer on each keystroke
  void OnSearchTimer(wxTimerEvent &event);   // runs the actual search after typing pauses
  void OnTreeMenu(wxTreeEvent &event);
  void OnTreeActivated(wxTreeEvent &event);
  void OnPopupClick(wxCommandEvent &evt);
  void Export(GameFile * file, int select);
  void UpdateInterface();

  // What picking a model row / a world model row does, callable without a tree event so the
  // headless self-test (-unityipctest with -wmo, and its lifecycle sequence) selects exactly as Browse.
  // raceID/sexID come from a race-browser leaf and say which race the model is to be read as;
  // -1 leaves that to the model (every ordinary file row). A world model is the Buildings viewer's: picking
  // or loading one switches to it (ModelViewer::SetViewerMode).
  void SelectModelFile(GameFile * file, int raceID = -1, int sexID = -1);
  void SelectWMOFile(GameFile * file);

  // Browse follows the viewer mode (ModelViewer::SetViewerMode, its only caller): the models in Models mode,
  // the textures in Textures mode, the world model roots in Buildings mode. Each mode keeps its own search, and
  // its tree comes back as it was left -- the same folders open, the same row at the top, the same row picked --
  // without listing its files again (the model and building trees are kept whole while another is shown).
  void FollowViewerMode(ViewerMode mode);
  // An arrow key repeating in the tree (true), or let go: the texture view waits to decode, and the
  // Buildings tree waits to load the row picked until the key is let go.
  void KeysRepeating(bool repeating);
  // Something new is on screen (ModelViewer::DisplayedContentChanged): the model or building tree's picked
  // row stays picked only while its file is what is loaded.
  void PickedRowFollowsLoad();
  // Textures mode's tree (TextureBrowse): told when a client load starts and ends, and when a
  // FileDataID looked up from its search has been read. (The building and model trees are told too.)
  bool ShowsTextures() const;
  bool ShowsBuildings() const;
  TextureBrowse * textureBrowse() const { return m_textures; }
  void TexturesClientLoadStarting();
  void TexturesClientLoaded();
  void TextureLookupResolved(int fileDataId, const QString & indexName);

  // THE BUILDINGS: the client's world model ROOTS, the files a world model is loaded from. A root holds MOHD
  // (the header) and names its group files -- the geometry, loaded with it -- by FileDataID in GFID; a group
  // file holds one MOGP and is no world model by itself. On the current client every group file is named after
  // its root: "<root>_NNN.wmo", or "<root>_NNN_lodN.wmo" for a lower level of detail, in the root's folder.
  // So a ".wmo" is a group file when its name has that form AND that root exists beside it; every other ".wmo"
  // is a root. (Roots can look like groups -- 11xt_rockbridge_003.wmo has no "11xt_rockbridge.wmo" -- and
  // "<name>_lod1.wmo" is a root of its own with its own groups: the name alone would hide both.) Checked
  // against the files' own first chunks on 12.1.0.69933. Made once per client (it walks the file index).
  static bool isWmoGroupFile(const QString & path, const std::map<QString, GameFile *> & index);
  struct BuildingIndex
  {
    std::vector<GameFile *> roots;               // in path order
    std::vector<std::pair<int, int>> byId;       // (FileDataID, index into roots), sorted; a client with FileDataIDs
    std::vector<std::pair<int, int>> groupsById; // (group FileDataID, index of its root), sorted
    size_t wmoFiles = 0;                         // every .wmo of the index
    size_t groupFiles = 0;
    bool built = false;
  };
  const BuildingIndex & buildingIndex() const { return m_buildings; }

  wxTreeCtrl *fileTree;
  wxSearchCtrl *txtContent;
  wxStaticText *searchStatus;
  wxTreeItemId CurrentItem;

  ModelViewer* modelviewer; // point to parent

public:
  // Unload the model or building on the canvas (and the textures it held). Also a newly loaded client's first step.
  void ClearCanvas();
private:
  // ONE KEPT TREE PER TREE MODE (Models, Buildings): the hierarchy last built, the search it was built with,
  // and -- while another mode has the tree -- the folders open, the row at the top and the row picked, so it
  // comes back as it was without listing the files again. A hierarchy that is replaced (another search, another
  // client) is retired and freed once no row of the tree points into it (clearRows).
  struct TreeState
  {
    TreeStackItem * root = nullptr;
    bool rootValid = false;   // root is the loaded client's, built with rootContent
    QString rootContent;
    wxString rootStatus;
    bool kept = false;
    std::set<TreeStackItem *> openNodes;
    TreeStackItem * topNode = nullptr;
    TreeStackItem * pickedNode = nullptr;
    // The browsing tree of the mode (no search), built once per client and kept across searches.
    TreeStackItem * browseRoot = nullptr;
    // A Buildings search of a FileDataID: its pinned row (no part of the hierarchy) was the row picked.
    bool pickedPinned = false;
  };
  TreeState & treeState() { return m_mode == ViewerMode::Buildings ? m_buildingsTree : m_modelsTree; }
  void keepTree(TreeState & state);
  bool reuseTree(TreeState & state, const QString & content);
  void reopenRows(TreeState & state, wxTreeItemId parent, wxTreeItemId & top, wxTreeItemId & picked);
  void forgetTrees();
  // A hierarchy no TreeState keeps any more: freed once no row points into it (its rows may still be on screen).
  void retireTree(TreeStackItem * root);
  // Every row of the tree goes; the hierarchies retired until now are then unreachable and are freed right after
  // the rebuild has shown its rows (a whole client's tree takes about 0.1 s to free). Every rebuild of the model
  // and building trees clears its rows through this.
  void clearRows();
  // Frees the hierarchies retired until now -- only when no row points into any of them -- after the current event.
  void freeRetiredLater();
  void freeTrees(const std::vector<TreeStackItem *> & roots);
  std::vector<TreeStackItem *> m_retired;
  // Whether a row's file is what is loaded now (a world model in Buildings, a model in Models).
  bool isLoaded(GameFile * file) const;
  // The search box's hint and tooltip: what the viewer mode's search finds.
  void UpdateSearchHint();
  // Each mode's Init, row picks and right-click menu.
  void InitModels(const QString & content);
  void InitBuildings(const QString & content);
  void InitTextures();
  void SelectTextureRow(wxTreeItemId item);
  void ShowTextureMenu(wxTreeItemId item);
  // A Buildings row picked: its root loaded, or a FileDataID looked up.
  void SelectBuildingRow(wxTreeItemId item);
  void LookUpBuilding(int fileDataId);
  // The world model roots, once per client (m_buildings).
  bool ensureBuildings();
  // A tree row for a building: its path, as the Models tree labels files, with its FileDataID.
  static QString buildingLabel(GameFile * file);
  // A Buildings search of a FileDataID: its row first -- the building with it, the building a group file with it
  // belongs to, or a row that looks it up -- with the note for the status line. Made with the tree, and again when
  // the tree comes back (reuseTree). False when the search is no FileDataID (or the client has none).
  bool pinFileDataIdRow(const QString & term, wxString & note);
  // The line under the tree: the minimum-length hint, the result count, or why nothing is listed.
  void SetSearchStatus(const wxString & text);

  // One-shot debounce for the as-you-type search: each keystroke restarts it, and the
  // expensive filter + tree rebuild (Init) runs only when it fires after a brief pause.
  wxTimer m_searchTimer;

  // What the tree lists: the viewer mode's files. Set only by FollowViewerMode.
  ViewerMode m_mode = ViewerMode::Models;
  // Textures mode's tree source, and a check from a client load's start until it has ended
  // (the load's own notice can come while it is still finishing, and a failed load sends none).
  TextureBrowse * m_textures = nullptr;
  wxTimer m_texturesLoadWatch;
  // Each mode's search: the text while another mode has the box, and the text its tree was last built with
  // (a short text typed but never run -- the search waits for 3 characters -- is not what it shows).
  wxString m_search[3];
  wxString m_applied[3];
  static int slot(ViewerMode mode) { return (int)mode; }
  TreeState m_modelsTree;
  TreeState m_buildingsTree;
  BuildingIndex m_buildings;
  bool m_restoringTree = false;   // reuseTree picking its row again: not a pick
  // An arrow key repeating in the Buildings tree: the rows it passes are not loaded until it is let go.
  bool m_keysRepeating = false;
  bool m_buildingPickPending = false;
};

#endif
