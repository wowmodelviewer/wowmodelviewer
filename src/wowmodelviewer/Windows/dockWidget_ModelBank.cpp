#include "dockWidget_ModelBank.h"

dockWidgetModelBank::dockWidgetModelBank(QWidget *parent)
	: QWidget(parent)
{
	qInfo("Initializing Model Bank Dock Widget...");
	ui.setupUi(this);
}