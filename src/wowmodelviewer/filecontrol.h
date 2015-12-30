#ifndef FILECONTROL_H
#define FILECONTROL_H

#include "FileTreeItem.h"

class ModelViewer;

#include <wx/string.h>
#include <wx/treectrl.h> // wxTreeItemId

class FileTreeData:public wxTreeItemData
{
public:
  GameFile * file;
  FileTreeData(GameFile * f): file(f) {}
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
	void OnTreeCollapsedOrExpanded(wxTreeEvent &event);
	void OnButton(wxCommandEvent &event);
	void OnChoice(wxCommandEvent &event);
	void OnTreeMenu(wxTreeEvent &event);
	void OnPopupClick(wxCommandEvent &evt);
	void Export(wxString val, int select);
	wxString ExportPNG(wxString val);
	void UpdateInterface();

	wxTreeCtrl *fileTree;
	wxButton *btnSearch;
	wxTextCtrl *txtContent;
	wxChoice *choFilter;
	int filterMode;
	wxTreeItemId CurrentItem;

	ModelViewer* modelviewer; // point to parent

private:
	void ClearCanvas();

	class TreeStackItem : public Container<TreeStackItem>
	{
	  public:
	    wxTreeItemId id;
	    GameFile * file;

	    TreeStackItem() : file(0) {}

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

	    void createTreeItems(wxTreeCtrl * tree)
	    {
	      for(std::map<QString, TreeStackItem *>::iterator it = m_childrenMap.begin();
	          it != m_childrenMap.end() ;
	          ++it)
	      {
	        TreeStackItem * child = it->second;
	        child->id = tree->AppendItem(id, it->second->name().toStdString(), -1, -1, ((it->second->file)?new FileTreeData(it->second->file):0));
	        child->createTreeItems(tree);

	      }
	    }

	  private:
	    std::map<QString, TreeStackItem *> m_childrenMap;
	};
};

#endif
