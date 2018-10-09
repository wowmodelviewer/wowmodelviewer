#pragma once
#include <qmainwindow.h>
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

private:
	Ui::wmvMainWindow ui;

	QActionGroup *lightingGroup, *canvasSizeGroup, *cameraGroup;

	void resizeDisplay(FrameResolutions resolution = FRAMERES_4x3_768);

private slots:
	// File Menu
	void on_actionReset_Layout_triggered();

	// Docked Widgets
	void on_actionShow_File_List_triggered();
	void on_actionShow_Animation_Controls_triggered();
	void on_actionShow_Character_Designer_triggered();
	void on_actionShow_Light_Controls_triggered();
	void on_actionShow_Model_Controls_triggered();
	void on_actionShow_Model_Bank_triggered();

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
};