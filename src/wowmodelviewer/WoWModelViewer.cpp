#include "WoWModelViewer.h"
#include <qtranslator.h>

WoWModelViewer::WoWModelViewer(QWidget *parent)
	: QMainWindow(parent)
{
	qInfo("Initializing Window...");
	ui.setupUi(this);

	// Hide what we don't want at the start.
	ui.widgetAnimation->hide();
	ui.widgetCharacterControl->hide();
	ui.widgetEquipment->hide();
	ui.widgetLighting->hide();
	ui.widgetModelBank->hide();
	ui.widgetModelControls->hide();

	// Create & initalize our docked widgets
	animationWidget = new dockWidgetAnimation(this);
	characterDesignerWidget = new dockWidgetCharacterDesigner(this);
	equipmentWidget = new dockWidgetEquipment(this);
	fileListWidget = new dockWidgetFileList(this);
	lightingWidget = new dockWidgetLighting(this);
	modelBankWidget = new dockWidgetModelBank(this);
	modelControls = new dockWidgetModelControls(this);

	// Assign our widgets to the UI
	ui.widgetAnimation->setWidget(animationWidget);
	ui.widgetCharacterControl->setWidget(characterDesignerWidget);
	ui.widgetEquipment->setWidget(equipmentWidget);
	ui.widgetFileList->setWidget(fileListWidget);
	ui.widgetLighting->setWidget(lightingWidget);
	ui.widgetModelBank->setWidget(modelBankWidget);
	ui.widgetModelControls->setWidget(modelControls);
	tabifyDockWidget(ui.widgetCharacterControl, ui.widgetEquipment);	// Docks these two widgets together into a tabbed widget

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

	// Languages
	// Note: We are specifically NOT translating these names.
	QAction *actionLang_enUS = new QAction("English");
	actionLang_enUS->setCheckable(true);
	actionLang_enUS->setChecked(true);
	actionLang_enUS->setData("en_US");
	QAction *actionLang_deDE = new QAction("Deutsch");
	actionLang_deDE->setCheckable(true);
	actionLang_deDE->setData("de_DE");
	QAction *actionLang_frFR = new QAction(QString::fromWCharArray(L"Français"));
	actionLang_frFR->setCheckable(true);
	actionLang_frFR->setData("fr_FR");
	QAction *actionLang_zhCN = new QAction(QString::fromWCharArray(L"普通话"));
	actionLang_zhCN->setCheckable(true);
	actionLang_zhCN->setData("zh_CN");
	QAction *actionLang_zhTW = new QAction(QString::fromWCharArray(L"繁體中文"));
	actionLang_zhTW->setCheckable(true);
	actionLang_zhTW->setData("zh_TW");
	
	ui.menuLanguage->addAction(actionLang_enUS);
	ui.menuLanguage->addAction(actionLang_deDE);
	ui.menuLanguage->addAction(actionLang_frFR);
	ui.menuLanguage->addAction(actionLang_zhCN);
	ui.menuLanguage->addAction(actionLang_zhTW);

	// Make Connections
	connect(ui.menuLanguage, SIGNAL(triggered(QAction *)), this, SLOT(setLanguage(QAction *)));
	connect(ui.actionRandomize_Character, &QAction::triggered, characterDesignerWidget, &dockWidgetCharacterDesigner::randomizeAppearance);
}

WoWModelViewer::~WoWModelViewer()
{
	lightingGroup->deleteLater();
	canvasSizeGroup->deleteLater();
	cameraGroup->deleteLater();

	animationWidget->deleteLater();
	characterDesignerWidget->deleteLater();
	equipmentWidget->deleteLater();
	fileListWidget->deleteLater();
	lightingWidget->deleteLater();
	modelBankWidget->deleteLater();
	modelControls->deleteLater();
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

	resize(width, height);
}

void WoWModelViewer::on_actionReset_Layout_triggered()
{
	// Re-initialize our layout to the way it was when we first started WMV.

	// Hide
	ui.widgetAnimation->hide();
	ui.widgetCharacterControl->hide();
	ui.widgetEquipment->hide();
	ui.widgetFileList->show();
	ui.widgetLighting->hide();
	ui.widgetModelBank->hide();
	ui.widgetModelControls->hide();

	// Reset Actions
	ui.actionShow_Animation_Controls->setChecked(false);
	ui.actionShow_Character_Designer->setChecked(false);
	ui.actionShow_Equipment_Selector->setChecked(false);
	ui.actionShow_File_List->setChecked(true);
	ui.actionShow_Light_Controls->setChecked(false);
	ui.actionShow_Model_Bank->setChecked(false);
	ui.actionShow_Model_Controls->setChecked(false);

	tabifyDockWidget(ui.widgetCharacterControl, ui.widgetEquipment);

	// Reposition

	// Set sizes
	resize(1024, 768);
}

void WoWModelViewer::updateTranslations(QString locale)
{
	if (installedTranslation != NULL)
	{
		QApplication::removeTranslator(installedTranslation);
	}
	if (locale != "en_US")
	{
		QTranslator *translator = new QTranslator();
		if (translator->load(":/Translations/" + locale + ".qm") == true)
		{
			QApplication::installTranslator(translator);
			installedTranslation = translator;
		}
		else {
			qWarning("Error installing locale: %s", qPrintable(locale));
			installedTranslation = NULL;
		}
	}
	ui.retranslateUi(this);
	animationWidget->retranslate();
	characterDesignerWidget->retranslate();
	equipmentWidget->retranslate();
	fileListWidget->retranslate();
	lightingWidget->retranslate();
	modelBankWidget->retranslate();
	modelControls->retranslate();
}

void WoWModelViewer::setTranslation(QString locale)
{
	auto a = ui.menuLanguage->actions();
	for (size_t i = 0; i < a.count(); i++)
	{
		if (a.at(i)->data().toString() == locale)
		{
			setLanguage(a.at(i));
			return;
		}
	}
}

void WoWModelViewer::setLanguage(QAction *action)
{
	QString locale = action->data().toString();
	auto a = ui.menuLanguage->actions();
	for (size_t i = 0; i < a.count(); i++)
	{
		a.at(i)->setChecked(false);
	}
	action->setChecked(true);
	updateTranslations(locale);
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

void WoWModelViewer::on_actionShow_Equipment_Selector_triggered()
{
	ui.widgetEquipment->setVisible(ui.actionShow_Equipment_Selector->isChecked());
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
