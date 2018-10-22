#include "dockWidget_ModelControls.h"

dockWidgetModelControls::dockWidgetModelControls(QWidget *parent)
	: QWidget(parent)
{
	qInfo("Initializing Model Controls Dock Widget...");
	ui.setupUi(this);
}