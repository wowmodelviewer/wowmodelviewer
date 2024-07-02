#pragma once

#include "FileTreeItem.h"

class ModelViewer;

#include <wx/string.h>
#include <wx/treectrl.h> // wxTreeItemId

class FileTreeData : public wxTreeItemData
{
public:
	GameFile* file;

	FileTreeData(GameFile* f): file(f)
	{
	}
};

class FileControl : public wxWindow
{
	DECLARE_CLASS(FileControl)
	DECLARE_EVENT_TABLE()

public:
	// Constructor + Deconstructor
	FileControl(wxWindow* parent, wxWindowID id);
	~FileControl();

	void Init(ModelViewer* mv = nullptr);
	void OnTreeSelect(wxTreeEvent& event);
	void OnTreeCollapsedOrExpanded(wxTreeEvent& event);
	void OnButton(wxCommandEvent& event);
	void OnChoice(wxCommandEvent& event);
	void OnTreeMenu(wxTreeEvent& event);
	void OnPopupClick(wxCommandEvent& evt);
	void Export(wxString val, int select);
	wxString ExportPNG(wxString val);
	void UpdateInterface();

	wxTreeCtrl* fileTree;
	wxButton* btnSearch;
	wxTextCtrl* txtContent;
	wxChoice* choFilter;
	int filterMode;
	wxTreeItemId CurrentItem;

	ModelViewer* modelviewer; // point to parent

private:
	void ClearCanvas();

	class TreeStackItem : public Container<TreeStackItem>
	{
	public:
		wxTreeItemId id;
		GameFile* file;

		TreeStackItem() : file(nullptr)
		{
		}

		TreeStackItem* getChildByName(QString name)
		{
			std::map<QString, TreeStackItem*>::iterator it = m_childrenMap.find(name);

			if (it != m_childrenMap.end())
				return it->second;

			return nullptr;
		}

		void onChildAdded(TreeStackItem* child)
		{
			m_childrenMap[child->name()] = child;
		}

		void createTreeItems(wxTreeCtrl* tree)
		{
			for (auto& it : m_childrenMap)
			{
				TreeStackItem* child = it.second;
				child->id = tree->AppendItem(id, it.second->name().toStdWString(), -1, -1,
				                             ((it.second->file) ? new FileTreeData(it.second->file) : nullptr));
				child->createTreeItems(tree);
			}
		}

	private:
		std::map<QString, TreeStackItem*> m_childrenMap;
	};
};
