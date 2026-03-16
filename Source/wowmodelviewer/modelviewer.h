#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#if defined(__WIN32__) && !defined(__WIN__)
#endif

#include <wx/aui/aui.h>
#include "modelcanvas.h"
#include "charcontrol.h"
#include "lightcontrol.h"
#include "modelcontrol.h"
#include "modelbankcontrol.h"
#include "filecontrol.h"
#include "glm/glm.hpp"

#include <functional>
#include <memory>
#include <vector>

class ExporterPlugin;
class ImporterPlugin;
class SettingsControl;

namespace WMVLog
{
	class Logger;
}

class ModelViewer : public wxFrame
{
	DECLARE_CLASS(ModelViewer)
	DECLARE_EVENT_TABLE()

	void OnStatusBarRefreshTimer(wxTimerEvent& event);
	wxTimer timer;

public:
	// Constructor + Deconstructor
	ModelViewer();
	virtual ~ModelViewer();

	// our class objects
	AnimControl* animControl;
	ModelCanvas* canvas;
	CharControl* charControl;
	LightControl* lightControl;
	ModelControl* modelControl;
	//SoundControl *soundControl;
	SettingsControl* settingsControl;
	ModelBankControl* modelbankControl;

	FileControl* fileControl;

	std::vector<ExporterPlugin*> m_exporters;
	std::vector<ImporterPlugin*> m_importers;

	//wxWidget objects
	wxMenuBar* menuBar;
	wxMenu *fileMenu, *charMenu, *charGlowMenu, *viewMenu, *optMenu, *lightMenu;
	wxColourData bgDialogData;

	// wxAUI - new docking lib (now part of wxWidgets 2.8.0)
	wxAuiManager interfaceManager;

	// Boolean flags
	bool isWoWLoaded;
	bool isModel;
	bool isChar;
	bool isWMO;
	bool isADT;
	bool initDB;

	// Initialising related functions
	void InitMenu();
	void InitObjects();
	void InitDocking();
	void InitDatabase(std::function<void(int, int)> progressCallback = nullptr);

	// Save and load various settings between sessions
	void LoadSession();
	void SaveSession();
	// Save and load the GUI layout
	void LoadLayout();
	void SaveLayout();
	void ResetLayout();

	void LoadModel(GameFile* f);
	void LoadItem(unsigned int displayID);
	void LoadNPC(unsigned int modelid);

	// Window GUI event related functions
	//void OnIdle();
	void OnClose(wxCloseEvent& event);
	void OnExit(wxCommandEvent& event);
	void UpdateCanvasStatus();
	void SetCanvasSize(uint32 sizex, uint32 sizey);

	// menu commands
	void OnToggleDock(wxCommandEvent& event);
	void OnToggleCommand(wxCommandEvent& event);
	void OnSetColor(wxCommandEvent& event);
	void OnLightMenu(wxCommandEvent& event);
	void OnCamMenu(wxCommandEvent& event);

	// Wrapper function for character stuff (forwards events to charcontrol)
	void OnSetEquipment(wxCommandEvent& event);
	void OnCharToggle(wxCommandEvent& event);

	void OnMount(wxCommandEvent& event);
	void OnLanguage(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);
	void OnCanvasSize(wxCommandEvent& event);
	//void OnTest(wxCommandEvent& event);
	void OnExport(wxCommandEvent& event);
	void OnExportOther(wxCommandEvent& event);

	void UpdateControls();

	void ModelInfo();

	glm::vec3 DoSetColor(const glm::vec3& defColor);

	void OnGameToggle(wxCommandEvent& event);
	void OnViewLog(wxCommandEvent& event);
	void OnUpdateListfile(wxCommandEvent& event);
	void OnUpdateEncryptionKeys(wxCommandEvent& event);
	bool DownloadListfile();
	bool DownloadEncryptionKeys();
	bool CheckAndUpdateSupportFiles();
	void LoadWoW();
};
