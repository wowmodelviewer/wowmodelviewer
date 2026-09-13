#ifndef FILECONTROL_H
#define FILECONTROL_H

#include "FileTreeItem.h"

class ModelViewer;

#include <wx/string.h>
#include <wx/treectrl.h> // wxTreeItemId
#include <wx/timer.h>    // wxTimer for debounced as-you-type search

class wxSearchCtrl;
class wxStaticText;

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

    TreeStackItem() : file(0), loaded(false) {}

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
  void OnChoice(wxCommandEvent &event);
  void OnTreeMenu(wxTreeEvent &event);
  void OnPopupClick(wxCommandEvent &evt);
  void Export(wxString val, int select);
  wxString ExportPNG(wxString val);
  void UpdateInterface();

  wxTreeCtrl *fileTree;
  wxSearchCtrl *txtContent;
  wxStaticText *searchStatus;
  wxChoice *choFilter;
  int filterMode;
  wxTreeItemId CurrentItem;

  ModelViewer* modelviewer; // point to parent

private:
  void ClearCanvas();
  // The line under the search box: the minimum-length hint, or the result count.
  void SetSearchStatus(const wxString & text);

  // Persistent file-tree hierarchy (rebuilt each Init/search). It must outlive
  // Init() so collapsed branches can be filled in lazily on expand.
  TreeStackItem * m_treeRoot;

  // One-shot debounce for the as-you-type search: each keystroke restarts it, and the
  // expensive filter + tree rebuild (Init) runs only when it fires after a brief pause.
  wxTimer m_searchTimer;
};

#endif
