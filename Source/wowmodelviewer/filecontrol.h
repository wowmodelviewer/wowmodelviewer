#ifndef FILECONTROL_H
#define FILECONTROL_H

#include "FileTreeItem.h"

class ModelViewer;

#include <wx/string.h>
#include <wx/treectrl.h> // wxTreeItemId
#include <wx/timer.h>    // wxTimer for debounced as-you-type search

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
  void Export(wxString val, int select);
  void UpdateInterface();

  // What picking a model row / a world model row does, callable without a tree event so the
  // headless self-test (-unityipctest with -wmo, and its lifecycle sequence) selects exactly as Browse.
  // raceID/sexID come from a race-browser leaf and say which race the model is to be read as;
  // -1 leaves that to the model (every ordinary file row).
  void SelectModelFile(GameFile * file, int raceID = -1, int sexID = -1);
  void SelectWMOFile(GameFile * file);

  // Browse follows the viewer mode (ModelViewer::SetViewerMode, its only caller): the models and world
  // models in Models mode, the textures in Textures mode. Each side keeps its own search, and its tree
  // comes back as it was left -- the same folders open, the same row at the top, the same row picked --
  // without listing its files again (the model tree is kept whole while the textures are shown).
  void FollowViewerMode(bool textures);
  // An arrow key repeating in the tree (true), or let go: told to the texture view.
  void KeysRepeating(bool repeating);
  // Something new is on screen (ModelViewer::DisplayedContentChanged): the model tree's picked row stays
  // picked only while its file is what is loaded.
  void PickedRowFollowsLoad();
  // Textures mode's tree (TextureBrowse): told when a client load starts and ends, and when a
  // FileDataID looked up from its search has been read.
  bool ShowsTextures() const;
  TextureBrowse * textureBrowse() const { return m_textures; }
  void TexturesClientLoadStarting();
  void TexturesClientLoaded();
  void TextureLookupResolved(int fileDataId, const QString & indexName);

  wxTreeCtrl *fileTree;
  wxSearchCtrl *txtContent;
  wxStaticText *searchStatus;
  wxTreeItemId CurrentItem;

  ModelViewer* modelviewer; // point to parent

private:
  void ClearCanvas();
  // The model tree left for the textures is kept, and comes back as it was -- rows rebuilt from the same
  // hierarchy, the same folders open, the same row at the top and picked -- without listing the files
  // again (which also left the old hierarchy behind, never freed).
  void keepModelTree();
  bool reuseModelTree();
  void reopenModelRows(wxTreeItemId parent, wxTreeItemId & top, wxTreeItemId & picked);
  // A world model (*.wmo) rather than a model (*.m2); whether a row's file is what is loaded now.
  static bool isWorldModel(GameFile * file);
  bool isLoaded(GameFile * file) const;
  // The search box's hint and tooltip: what the viewer mode's search finds.
  void UpdateSearchHint();
  // Textures mode's Init, row picks and right-click menu.
  void InitTextures();
  void SelectTextureRow(wxTreeItemId item);
  void ShowTextureMenu(wxTreeItemId item);
  // The line under the tree: the minimum-length hint, the result count, or why nothing is listed.
  void SetSearchStatus(const wxString & text);

  // Persistent file-tree hierarchy (rebuilt each Init/search). It must outlive
  // Init() so collapsed branches can be filled in lazily on expand.
  TreeStackItem * m_treeRoot;

  // One-shot debounce for the as-you-type search: each keystroke restarts it, and the
  // expensive filter + tree rebuild (Init) runs only when it fires after a brief pause.
  wxTimer m_searchTimer;

  // What the tree lists: the textures (Textures mode) or the models (Models mode). Set only by
  // FollowViewerMode, so it is always the viewer mode's.
  bool m_showsTextures = false;
  // Textures mode's tree source, and a check from a client load's start until it has ended
  // (the load's own notice can come while it is still finishing, and a failed load sends none).
  TextureBrowse * m_textures = nullptr;
  wxTimer m_texturesLoadWatch;
  wxString m_texturesSearch;   // the textures' search text while the models are shown
  wxString m_modelsSearch;     // the models' while the textures are
  wxString m_texturesApplied;  // the search each tree was last built with (a short text typed but never
  wxString m_modelsApplied;    // run -- the search waits for 3 characters -- is not what it shows)
  // The model tree as it was when the textures took the tree (keepModelTree).
  bool m_treeRootValid = false;   // m_treeRoot is the loaded client's, built with this search
  QString m_treeRootContent;
  wxString m_treeRootStatus;
  bool m_modelTreeKept = false;
  std::set<TreeStackItem *> m_modelOpenNodes;
  TreeStackItem * m_modelTopNode = nullptr;
  TreeStackItem * m_modelPickedNode = nullptr;
  bool m_restoringTree = false;   // reuseModelTree picking its row again: not a pick
};

#endif
