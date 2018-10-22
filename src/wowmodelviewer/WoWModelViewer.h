#pragma once
#include <qmainwindow.h>
#include "Windows/dockWidget_Animation.h"
#include "Windows/dockWidget_CharacterDesigner.h"
#include "Windows/dockWidget_Equipment.h"
#include "Windows/dockWidget_FileList.h"
#include "Windows/dockWidget_Lighting.h"
#include "Windows/dockWidget_ModelBank.h"
#include "Windows/dockWidget_ModelControls.h"
#include "ui_wmvMainWindow.h"

class WoWModelViewer : public QMainWindow
{
private:
	Q_OBJECT
	enum FrameResolutions {
		FRAMERES_2160p = 0,
		FRAMERES_1080p,
		FRAMERES_720p,
		FRAMERES_1x1_128,
		FRAMERES_1x1_256,
		FRAMERES_1x1_512,
		FRAMERES_1x1_1024,
		FRAMERES_1x1_2048,
		FRAMERES_4x3_480,
		FRAMERES_4x3_600,
		FRAMERES_4x3_768,
		FRAMERES_4x3_1024,
		FRAMERES_16x9_480,
		FRAMERES_16x9_720,
		FRAMERES_16x9_1080,
		FRAMERES_16x9_1440,
		FRAMERES_16x9_2160,
		FRAMERES_MISC_768,
		FRAMERES_MISC_1200,
	};

public:
	WoWModelViewer(QWidget *parent = 0);
	~WoWModelViewer();

	void updateTranslations(QString locale);
	void setTranslation(QString locale);

	void setStatusVersion(QString value);
	void setStatusLocale(QString value);

public slots:
	void setStatusMessage(QString message, int timeout = 0);		// Timeout is in Milliseconds.

private:
	Ui::wmvMainWindow ui;
	QTranslator* installedTranslation = NULL;

	QActionGroup *lightingGroup, *canvasSizeGroup, *cameraGroup, *eyeGlowGroup;

	dockWidgetAnimation* animationWidget;
	dockWidgetCharacterDesigner* characterDesignerWidget;
	dockWidgetEquipment* equipmentWidget;
	dockWidgetFileList* fileListWidget;
	dockWidgetLighting* lightingWidget;
	dockWidgetModelBank* modelBankWidget;
	dockWidgetModelControls* modelControls;


	QLabel* statusBarVersion;
	QWidget *statusBarLocale;
	QLabel* statusBarLocaleText;
	QLabel* statusBarLocaleFlag;

	void resizeDisplay(FrameResolutions resolution = FRAMERES_4x3_768);
	void LoadWoW();

private slots:
	// File Menu
	void on_actionLoad_World_of_Warcraft_triggered();
	void on_actionView_Log_triggered();
	void on_actionSave_Screenshot_triggered();
	void on_actionSave_Sized_Screenshot_triggered() {};
	void on_actionGIF_Sequence_Export_triggered() {};
	void on_actionExport_AVI_triggered() {};
	void on_actionExport_ModelInfo_xml_triggered() {};
	void on_actionReset_Layout_triggered();

	// View
	void on_actionView_NPC_triggered() {};
	void on_actionView_Item_triggered() {};
	void on_actionShow_File_List_triggered();
	void on_actionShow_Animation_Controls_triggered();
	void on_actionShow_Character_Designer_triggered();
	void on_actionShow_Equipment_Selector_triggered();
	void on_actionShow_Light_Controls_triggered();
	void on_actionShow_Model_Controls_triggered();
	void on_actionShow_Model_Bank_triggered();
	void on_actionBackground_Color_triggered() {};
	void on_actionLoad_Background_triggered() {};
	void on_actionSkybox_triggered() {};
	void on_actionShow_Grid_triggered() {};
	void on_actionShow_Mask_triggered() {};

	// Camera
	void on_actionCamera_UseModel_triggered() {};
	void on_actionCameraPerspective_triggered() {};
	void on_actionCameraFront_triggered() {};
	void on_actionCameraBack_triggered() {};
	void on_actionCameraLeft_triggered() {};
	void on_actionCameraRight_triggered() {};
	void on_actionCameraTop_triggered() {};

	// Resolutions
	void on_action2160p_triggered()			{ resizeDisplay(FRAMERES_2160p); };
	void on_action1080p_triggered()			{ resizeDisplay(FRAMERES_1080p); };
	void on_action720p_triggered()			{ resizeDisplay(FRAMERES_720p); };
	void on_action128_x_128_triggered()		{ resizeDisplay(FRAMERES_1x1_128); };
	void on_action256_x_256_triggered()		{ resizeDisplay(FRAMERES_1x1_256); };
	void on_action512_x_512_triggered()		{ resizeDisplay(FRAMERES_1x1_512); };
	void on_action1024_x_1024_triggered()	{ resizeDisplay(FRAMERES_1x1_1024); };
	void on_action2048_x_2048_triggered()	{ resizeDisplay(FRAMERES_1x1_2048); };
	void on_action640_x_480_triggered()		{ resizeDisplay(FRAMERES_4x3_480); };
	void on_action800_x_600_triggered()		{ resizeDisplay(FRAMERES_4x3_600); };
	void on_action1024_x_768_triggered()	{ resizeDisplay(FRAMERES_4x3_768); };
	void on_action1280_x_1024_triggered()	{ resizeDisplay(FRAMERES_4x3_1024); };
	void on_action864_x_480_triggered()		{ resizeDisplay(FRAMERES_16x9_480); };
	void on_action1280_x_720_triggered()	{ resizeDisplay(FRAMERES_16x9_720); };
	void on_action1920_x_1080_triggered()	{ resizeDisplay(FRAMERES_16x9_1080); };
	void on_action2560_x_1440_triggered()	{ resizeDisplay(FRAMERES_16x9_1440); };
	void on_action3840_x_2160_triggered()	{ resizeDisplay(FRAMERES_16x9_2160); };
	void on_action1280_x_768_triggered()	{ resizeDisplay(FRAMERES_MISC_768); };
	void on_action1920_x_1200_triggered()	{ resizeDisplay(FRAMERES_MISC_1200); };

	// Character
	void on_actionLoad_Character_triggered() {};
	void on_actionSave_Character_triggered() {};
	void on_actionImport_Armory_Character_triggered() {};
	void on_actionEyeGlowNone_triggered() {};
	void on_actionEyeGlowDefault_triggered() {};
	void on_actionEyeGlowDeathKnight_triggered() {};
	void on_actionCharacterShowUnderwear_triggered() {};
	void on_actionCharacterShowEars_triggered() {};
	void on_actionCharacterShowHair_triggered() {};
	void on_actionCharacterShowFacialHair_triggered() {};
	void on_actionCharacterShowFeet_triggered() {};
	void on_actionCharacterAutoHideGeosets_triggered() {};
	void on_actionCharacterSheatheWeapons_triggered() {};
	void on_actionLoadEquipment_triggered() {};
	void on_actionSaveEquipment_triggered() {};
	void on_actionClearEquipment_triggered() {};
	void on_actionLoadItemSet_triggered() {};
	void on_actionLoadStartingOutfit_triggered() {};
	void on_actionMount_Dismount_triggered() {};

	// Lighting
	void on_actionLoad_Lighting_triggered() {};
	void on_actionSave_Lighting_triggered() {};
	void on_actionRender_Light_Objects_triggered() {};
	void on_actionUse_True_lighting_triggered() {};
	void on_actionUse_Dynamic_lighting_triggered() {};
	void on_actionUse_Ambient_Lighting_triggered() {};
	void on_actionModel_Lights_only_triggered() {};

	// Options
	void on_actionAlwaysShowDefaultDoodadsInWMOs_triggered() {};
	void on_actionSettings_triggered();

	// Effects
	void on_actionApply_Enchants_triggered() {};

	// About Menu
	void setLanguage(QAction *action);
	void on_actionHelp_triggered() {};
	void on_actionAbout_triggered() {};
};