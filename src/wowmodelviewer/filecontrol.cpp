#include "logger/Logger.h"

#include <wx/filename.h>
#include <wx/msgdlg.h>

#include <QDirIterator>
#include <QImage>

#include "CASCFile.h"
#include "Game.h"
#include "globalvars.h"
#include "logger/Logger.h"
#include "modelviewer.h"

IMPLEMENT_CLASS(FileControl, wxWindow)

BEGIN_EVENT_TABLE(FileControl, wxWindow)
	// model tree
	EVT_TREE_SEL_CHANGED(ID_FILELIST, FileControl::OnTreeSelect)
	EVT_TREE_ITEM_EXPANDED(ID_FILELIST, FileControl::OnTreeCollapsedOrExpanded)
	EVT_TREE_ITEM_COLLAPSED(ID_FILELIST, FileControl::OnTreeCollapsedOrExpanded)
	EVT_BUTTON(ID_FILELIST_SEARCH, FileControl::OnButton)
	EVT_TEXT_ENTER(ID_FILELIST_CONTENT, FileControl::OnButton)
	EVT_CHOICE(ID_FILELIST_FILTER, FileControl::OnChoice)
	EVT_TREE_ITEM_MENU(ID_FILELIST, FileControl::OnTreeMenu)
END_EVENT_TABLE()

enum FilterModes {
	FILE_FILTER_MODEL=0,
	FILE_FILTER_WMO,
	FILE_FILTER_ADT,
	FILE_FILTER_WAV,
	FILE_FILTER_MP3,
	FILE_FILTER_IMAGE,
	FILE_FILTER_BLS,
	FILE_FILTER_DBC,
	FILE_FILTER_DB2,
	FILE_FILTER_LUA,
	FILE_FILTER_XML,

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
	"blp", "bls", "dbc", "db2", "lua", "xml", "skin"};
static wxString chos[] = {wxT("Models (*.m2)"), wxT("WMOs (*.wmo)"), wxT("ADTs (*.adt)"), wxT("WAVs (*.wav)"), wxT("OGGs (*.ogg)"), wxT("MP3s (*.mp3)"), 
	wxT("Images (*.blp)"), wxT("Shaders (*.bls)"), wxT("DBCs (*.dbc)"), wxT("DB2s (*.db2)"), wxT("LUAs (*.lua)"), wxT("XMLs (*.xml)"), wxT("SKINs (*.skin)")};

void beautifyFileName(QString & file)
{
  file = file.toLower();
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

	if (Create(parent, id, wxDefaultPosition, wxSize(170,700), 0, wxT("ModelControlFrame")) == false) {
		LOG_ERROR << "Failed to create a window for our FileControl!";
		return;
	}

	try {
		txtContent = new wxTextCtrl(this, ID_FILELIST_CONTENT, wxEmptyString, wxPoint(10, 10), wxSize(110, 20), wxTE_PROCESS_ENTER, wxDefaultValidator);
		btnSearch = new wxButton(this, ID_FILELIST_SEARCH, _("Clear"), wxPoint(120, 10), wxSize(46,20));
		fileTree = new wxTreeCtrl(this, ID_FILELIST, wxPoint(0, 35), wxSize(250,580), wxTR_HIDE_ROOT|wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT|wxTR_FULL_ROW_HIGHLIGHT|wxTR_NO_LINES);
		choFilter = new wxChoice(this, ID_FILELIST_FILTER, wxPoint(10, 620), wxSize(130, 10), WXSIZEOF(chos), chos);
		choFilter->SetSelection(filterMode);
	} catch(...) {};
}

FileControl::~FileControl()
{
	if (fileTree) {
		fileTree->Destroy();
		fileTree = NULL;
	}
	txtContent->Destroy();
	btnSearch->Destroy();
	choFilter->Destroy();
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

	LOG_INFO << "Initializing File Controls - Start";

	// Gets the list of files that meet the filter criteria
	// and puts them into an array to be processed into our file tree
	content = QString(txtContent->GetValue().c_str()).toLower().trimmed();
	filterString = "^.*"+ content +".*\\." + filterStrings[filterMode];
	std::set<GameFile *> files;
	GAMEDIRECTORY.getFilteredFiles(files, filterString);

	LOG_INFO << "Initializing File Controls - Filtering done - files found" << files.size();
	TreeStackItem root;
	for (std::set<GameFile *>::iterator it = files.begin(); it != files.end(); ++it) {
	  QString name = (*it)->fullname();
	  beautifyFileName(name);

	  QStringList items = name.split("\\");
	  TreeStackItem * curparent = &root;
	  for(int i=0; i < items.size() -1; i++)
	  {
	    TreeStackItem * child = curparent->getChildByName(items[i]);
	    if(!child)
	    {
	      child = new TreeStackItem();
	      child->setName(items[i]);
	      curparent->addChild(child);
	    }
	    curparent = child;
	  }
	  TreeStackItem * child = new TreeStackItem();
	  child->file = *it;
	  child->setName(items[items.size()-1]);
	  curparent->addChild(child);
	}

	LOG_INFO << "Initializing File Controls - File Hierarchy created";
	fileTree->DeleteAllItems();
	root.id = fileTree->AddRoot(wxT("Root"));
	root.createTreeItems(fileTree);

	LOG_INFO << "Initializing File Controls - END";

	if (content != wxEmptyString)
		fileTree->ExpandAll();
}

void FileControl::OnChoice(wxCommandEvent &event)
{
	int id = event.GetId();
	if (id == ID_FILELIST_FILTER) {
		int curSelection = choFilter->GetCurrentSelection();
		if (curSelection >= 0 && curSelection != filterMode) {
			filterMode = curSelection;
			Init();
		}
	}
}

// copy from ModelOpened::Export
void FileControl::Export(wxString val, int select)
{
	if (val.IsEmpty())
		return;

	GameFile * f = GAMEDIRECTORY.getFile(val.c_str());
	if(!f)
	{
	  LOG_ERROR << "Could not extract" << val.c_str();
	  return;
	}

	f->open();

	if (f->isEof())
	{
		LOG_ERROR << "Could not extract" << val.c_str();
		f->close();
		return;
	}

	LOG_INFO << "Saving" << val.c_str();

	wxFileName fn = fixMPQPath(val);

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

	LOG_INFO << "Saving to" << filename.c_str();

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
	  LOG_ERROR << "Saving to" << filename.c_str() << "failed";
	}

	f->close();
}

wxString FileControl::ExportPNG(wxString val)
{
	if (val.IsEmpty())
		return "";

	wxFileName fn(val);
	if (fn.GetExt().Lower() != wxT("blp"))
		return "";

	TextureID temptex = texturemanager.add(GAMEDIRECTORY.getFile(val.c_str()));
	Texture &tex = *((Texture*)texturemanager.items[temptex]);
	if (tex.w == 0 || tex.h == 0)
		return "";

	wxString filename;
	filename = wxFileSelector(wxT("Save PNG ..."), wxGetCwd(), fn.GetName(), "png",wxT("PNG Files (.png)|*.png"));

	if ( filename.empty() ){
		filename = wxGetCwd()+SLASH+wxT("Export")+SLASH+fn.GetName()+wxT(".png");
	}

	unsigned char *tempbuf = (unsigned char*)malloc(tex.w*tex.h*4);
	tex.getPixels(tempbuf, GL_BGRA_EXT);

	QImage PNGFile(tempbuf, tex.w, tex.h, QImage::Format_RGBA8888);
	PNGFile.save(filename.mb_str());

	free(tempbuf);
	return filename;
}

void FileControl::OnPopupClick(wxCommandEvent &evt)
{
	FileTreeData *data = (FileTreeData*)(static_cast<wxMenu *>(evt.GetEventObject())->GetClientData());
	wxString val(data->file->fullname().toStdString());

	int id = evt.GetId();
	if (id == ID_FILELIST_SAVE) { 
		Export(val, 1);
	} else if (id == ID_FILELIST_VIEW) {
	  wxString temp = ExportPNG(val);

	  if(!temp.IsEmpty())
	  {
	    ScrWindow *sw = new ScrWindow(temp);
	    sw->Show(true);
	  }
	  else
	  {
	    wxMessageDialog dial(NULL, wxT("Error during file export."));
	    dial.ShowModal();
	  }
	}
}

void FileControl::OnTreeMenu(wxTreeEvent &event)
{
	wxTreeItemId item = event.GetItem();

	if (!item.IsOk() || !modelviewer->canvas) // make sure that a valid Tree Item was actually selected.
		return;

	void *data = reinterpret_cast<void *>(fileTree->GetItemData(item));
	FileTreeData *tdata = (FileTreeData*)data;

	// make sure the data (file name) is valid
	if (!data)
		return; // isn't valid, exit.
	
	// Make a menu to show item Info or export it
	wxMenu infoMenu;
	infoMenu.SetClientData( data );
	infoMenu.Append(ID_FILELIST_SAVE, wxT("&Save..."), wxT("Save this object"));
	// TODO: if is music, a Play option
	wxString temp(tdata->file->fullname().toStdString());
	temp.MakeLower();

	// if is graphic, a View option
	if (temp.EndsWith(wxT("blp")))
		infoMenu.Append(ID_FILELIST_VIEW, wxT("&View"), wxT("View this object"));

	infoMenu.Connect(wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction)&FileControl::OnPopupClick, NULL, this);
	PopupMenu(&infoMenu);
}

void FileControl::ClearCanvas()
{
	if (!modelviewer->isModel && !modelviewer->isWMO && !modelviewer->isADT)
		return;

	// Delete any previous models that were loaded.
	if (modelviewer->isWMO) {
		//canvas->clearAttachments();
		wxDELETE(modelviewer->canvas->wmo);
		modelviewer->canvas->wmo = NULL;
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
		modelviewer->canvas->model = NULL;
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
	texturemanager.clear();

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
	if (modelviewer->isModel == true){
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
		modelviewer->charMenu->Enable(ID_CHAR_RANDOMISE, true);
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
		modelviewer->charMenu->Enable(ID_CHAR_RANDOMISE, false);
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
		modelviewer->charMenu->Enable(ID_CHAR_RANDOMISE, false);
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
		modelviewer->charMenu->Enable(ID_CHAR_RANDOMISE, false);
		modelviewer->charMenu->Enable(ID_AUTOHIDE_GEOSETS_FOR_HEAD_ITEMS, false);
	}

	// Update the layout
	modelviewer->interfaceManager.Update();
}

void FileControl::OnTreeSelect(wxTreeEvent &event)
{
	wxTreeItemId item = event.GetItem();

	// make sure that a valid Tree Item was actually selected.
	if (!item.IsOk() || !modelviewer->canvas){
		return;
	}

	FileTreeData *data = (FileTreeData*)fileTree->GetItemData(item);

	// make sure the data (file name) is valid
	if (!data || !data->file){
		return; // isn't valid, exit.
	}

	CurrentItem = item;

	if (filterMode == FILE_FILTER_MODEL) {
	  wxString rootfn(data->file->fullname().toStdString());
		// Exit, if its the same model thats currently loaded
		if (modelviewer->canvas->model && !modelviewer->canvas->model->name().isEmpty() && modelviewer->canvas->model->name().toStdString() == std::string(rootfn.c_str()))
			return; // clicked on the same model thats currently loaded, no need to load it again - exit

		ClearCanvas();
		LOG_INFO << "Selecting model in tree selector:" << rootfn.c_str();

		// Check to make sure the selected item is a model (an *.m2 file).
		modelviewer->isModel = (rootfn.Last() == '2');

		// not functional yet.
		//if (wxGetKeyState(WXK_SHIFT)) 
		//	canvas->AddModel(rootfn);
		//else
			modelviewer->LoadModel(GAMEDIRECTORY.getFile(rootfn.c_str()));	// Load the model.

		UpdateInterface();
	} else if (filterMode == FILE_FILTER_WMO) {
		ClearCanvas();

		modelviewer->isWMO = true;
		wxString rootfn(data->file->fullname().toStdString());

		//canvas->model->modelType = MT_WMO;

		// if we have selected a non-root wmo, find the root filename
		char dash = rootfn[rootfn.length() - 8];
		char num = rootfn[rootfn.length() - 7];
		bool isroot = !((dash=='_') && (num>='0') && (num<='9'));
		if (!isroot) {
			rootfn.erase(rootfn.length()-8);
			rootfn.append(wxT(".wmo"));
		}

		modelviewer->canvas->LoadWMO(rootfn);

		int id = -1;
		if (!isroot) {
			char idnum[4];
			strncpy(idnum, (char *)rootfn.c_str() + strlen((char *)rootfn.c_str())-7,3);
			//wxString(data->fn.Substr((data->fn.Length() - 7), 3)).ToLong(&id);
			idnum[3]=0;
			sscanf(idnum,"%d",&id);
		}
		modelviewer->canvas->wmo->loadGroup(id);
		modelviewer->canvas->ResetViewWMO(id);
		modelviewer->animControl->UpdateWMO(modelviewer->canvas->wmo, id);

		// wxAUI
		modelviewer->interfaceManager.GetPane(modelviewer->charControl).Show(false);

		UpdateInterface();
	} else if (filterMode == FILE_FILTER_IMAGE) {
		ClearCanvas();

		// For Graphics
		wxString val(data->file->fullname().toStdString());
		ExportPNG(val);
		wxFileName fn(val);
		wxString temp(wxGetCwd()+SLASH+wxT("Export")+SLASH+fn.GetName()+wxT(".png"));
		modelviewer->canvas->LoadBackground(temp);
		wxRemoveFile(temp);

		UpdateInterface();
	} else if (filterMode == FILE_FILTER_ADT) {
		ClearCanvas();

		modelviewer->isADT = true;
		wxString rootfn(data->file->fullname().toStdString());
		modelviewer->canvas->LoadADT(rootfn);

		UpdateInterface();
	} else {
		ClearCanvas();

		UpdateInterface();
	}
}

// bg recolor
void FileControl::OnTreeCollapsedOrExpanded(wxTreeEvent &)
{
	wxTreeItemId h;
	size_t i = 0;
	for(h=fileTree->GetFirstVisibleItem();h.IsOk();h=fileTree->GetNextVisible(h)) {
		if (!fileTree->IsVisible(h))
			break;
		if (i++%2==1)
			fileTree->SetItemBackgroundColour(h, wxColour(237,243,254));
		else
			fileTree->SetItemBackgroundColour(h, *wxWHITE);
	}
}

void FileControl::OnButton(wxCommandEvent &event)
{
	int id = event.GetId();
	if (id == ID_FILELIST_CONTENT)
		Init();
	else if (id == ID_FILELIST_SEARCH) {
		txtContent->SetValue(wxEmptyString);
		Init();
	}
}



