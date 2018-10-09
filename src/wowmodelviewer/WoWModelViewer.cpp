#include "WoWModelViewer.h"
#include "dockWidget_Animation.h"
#include "dockWidget_CharacterDesigner.h"
#include "dockWidget_FileList.h"
#include "dockWidget_Lighting.h"
#include "dockWidget_ModelBank.h"
#include "dockWidget_ModelControls.h"

WoWModelViewer::WoWModelViewer(QWidget *parent)
	: QMainWindow(parent)
{
	qInfo("Initializing Window...");
	ui.setupUi(this);

	// Hide what we don't want at the start.
	ui.widgetAnimation->hide();
	ui.widgetLighting->hide();
	ui.widgetModelBank->hide();
	ui.widgetModelControls->hide();

	// Create & initalize our docked widgets
	ui.widgetAnimation->setWidget(new dockWidgetAnimation(this));
	ui.widgetCharacterControl->setWidget(new dockWidgetCharacterDesigner(this));
	ui.widgetFileList->setWidget(new dockWidgetFileList(this));
	ui.widgetLighting->setWidget(new dockWidgetLighting(this));
	ui.widgetModelBank->setWidget(new dockWidgetModelBank(this));
	ui.widgetModelControls->setWidget(new dockWidgetModelControls(this));

	// UI Selection Groups
	lightingGroup = new QActionGroup(this);
	lightingGroup->setExclusive(true);
	lightingGroup->addAction(ui.actionUse_Dynamic_lighting);
	lightingGroup->addAction(ui.actionUse_Ambient_Lighting);
	lightingGroup->addAction(ui.actionModel_Lights_only);

	canvasSizeGroup = new QActionGroup(this);
	canvasSizeGroup->setExclusive(true);
	// Common Film
	canvasSizeGroup->addAction(ui.action2160p);
	canvasSizeGroup->addAction(ui.action1080p);
	canvasSizeGroup->addAction(ui.action720p);
	// 1:1
	canvasSizeGroup->addAction(ui.action128_x_128);
	canvasSizeGroup->addAction(ui.action256_x_256);
	canvasSizeGroup->addAction(ui.action512_x_512);
	canvasSizeGroup->addAction(ui.action1024_x_1024);
	canvasSizeGroup->addAction(ui.action2048_x_2048);
	// 16:9
	canvasSizeGroup->addAction(ui.action864_x_480);
	canvasSizeGroup->addAction(ui.action1280_x_720);
	canvasSizeGroup->addAction(ui.action1920_x_1080);
	canvasSizeGroup->addAction(ui.action2560_x_1440);
	canvasSizeGroup->addAction(ui.action3840_x_2160);
	// 4:3
	canvasSizeGroup->addAction(ui.action640_x_480);
	canvasSizeGroup->addAction(ui.action800_x_600);
	canvasSizeGroup->addAction(ui.action1024_x_768);
	canvasSizeGroup->addAction(ui.action1280_x_1024);
	// Misc
	canvasSizeGroup->addAction(ui.action1280_x_768);
	canvasSizeGroup->addAction(ui.action1920_x_1200);

	cameraGroup = new QActionGroup(this);
	cameraGroup->setExclusive(true);
	cameraGroup->addAction(ui.actionCamera_UseModel);
	cameraGroup->addAction(ui.actionCameraPerspective);
	cameraGroup->addAction(ui.actionCameraFront);
	cameraGroup->addAction(ui.actionCameraBack);
	cameraGroup->addAction(ui.actionCameraLeft);
	cameraGroup->addAction(ui.actionCameraRight);
	cameraGroup->addAction(ui.actionCameraTop);
}

WoWModelViewer::~WoWModelViewer()
{
	lightingGroup->deleteLater();
	canvasSizeGroup->deleteLater();
	cameraGroup->deleteLater();
}

void WoWModelViewer::resizeDisplay(FrameResolutions resolution)
{
	int width = 0;
	int height = 0;

	switch (resolution)
	{
	case WoWModelViewer::FRAMERES_1x1_128:
		width = height = 128;
		break;
	case WoWModelViewer::FRAMERES_1x1_256:
		width = height = 256;
		break;
	case WoWModelViewer::FRAMERES_1x1_512:
		width = height = 512;
		break;
	case WoWModelViewer::FRAMERES_1x1_1024:
		width = height = 1024;
		break;
	case WoWModelViewer::FRAMERES_1x1_2048:
		width = height = 2048;
		break;
	case WoWModelViewer::FRAMERES_4x3_480:
		width = 640;
		height = 480;
		break;
	case WoWModelViewer::FRAMERES_4x3_600:
		width = 800;
		height = 600;
		break;
	case WoWModelViewer::FRAMERES_4x3_1024:
		width = 1280;
		height = 1024;
		break;
	case WoWModelViewer::FRAMERES_16x9_480:
		width = 864;
		height = 480;
		break;
	case WoWModelViewer::FRAMERES_720p:
	case WoWModelViewer::FRAMERES_16x9_720:
		width = 1280;
		height = 720;
		break;
	case WoWModelViewer::FRAMERES_1080p:
	case WoWModelViewer::FRAMERES_16x9_1080:
		width = 1920;
		height = 1080;
		break;
	case WoWModelViewer::FRAMERES_16x9_1440:
		width = 2560;
		height = 1440;
		break;
	case WoWModelViewer::FRAMERES_2160p:
	case WoWModelViewer::FRAMERES_16x9_2160:
		width = 3840;
		height = 2160;
		break;
	case WoWModelViewer::FRAMERES_MISC_768:
		width = 1280;
		height = 768;
		break;
	case WoWModelViewer::FRAMERES_MISC_1200:
		width = 1920;
		height = 1200;
		break;
	case WoWModelViewer::FRAMERES_4x3_768:
	default:
		width = 1024;
		height = 768;
		break;
	}

	if (width == 0 || height == 0)
	{
		qCritical("Unable to get valid width or height. Aborting resize.");
		return;
	}

	// Character Designer
	if (height < 675 && ui.widgetCharacterControl->isFloating() == false)
	{
		ui.widgetCharacterControl->setFloating(true);
	}
	if (ui.widgetCharacterControl->isVisible() == true && ui.widgetCharacterControl->isFloating() == false)
	{
		width += ui.widgetCharacterControl->width();
	}

	// File Control
	if (ui.widgetFileList->isVisible() == true && ui.widgetFileList->isFloating() == false)
	{
		if (height < 140)
			ui.widgetFileList->setFloating(true);
	}
	if (ui.widgetFileList->isVisible() == true && ui.widgetFileList->isFloating() == false)
	{
		Qt::DockWidgetArea area = dockWidgetArea(ui.widgetFileList);
		if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
		{
			width += ui.widgetFileList->width();
		}
		if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea)
		{
			height += ui.widgetFileList->height();
		}
	}

	resize(width, height);
}

void WoWModelViewer::on_actionReset_Layout_triggered()
{
	// Re-initialize our layout to the way it was when we first started WMV.

	// Hide
	ui.widgetAnimation->hide();
	ui.widgetCharacterControl->show();
	ui.widgetFileList->show();
	ui.widgetLighting->hide();
	ui.widgetModelBank->hide();
	ui.widgetModelControls->hide();

	// Reset Actions
	ui.actionShow_Animation_Controls->setChecked(false);
	ui.actionShow_Character_Designer->setChecked(true);
	ui.actionShow_File_List->setChecked(true);
	ui.actionShow_Light_Controls->setChecked(false);
	ui.actionShow_Model_Bank->setChecked(false);
	ui.actionShow_Model_Controls->setChecked(false);

	// Reposition

	// Set sizes
	resize(1024, 768);
}

void WoWModelViewer::on_actionShow_File_List_triggered()
{
	ui.widgetFileList->setVisible(ui.actionShow_File_List->isChecked());
}

void WoWModelViewer::on_actionShow_Animation_Controls_triggered()
{
	ui.widgetAnimation->setVisible(ui.actionShow_Animation_Controls->isChecked());
}

void WoWModelViewer::on_actionShow_Character_Designer_triggered()
{
	ui.widgetCharacterControl->setVisible(ui.actionShow_Character_Designer->isChecked());
}

void WoWModelViewer::on_actionShow_Light_Controls_triggered()
{
	ui.widgetLighting->setVisible(ui.actionShow_Light_Controls->isChecked());
}

void WoWModelViewer::on_actionShow_Model_Controls_triggered()
{
	ui.widgetModelControls->setVisible(ui.actionShow_Model_Controls->isChecked());
}

void WoWModelViewer::on_actionShow_Model_Bank_triggered()
{
	ui.widgetModelBank->setVisible(ui.actionShow_Model_Bank->isChecked());
}
