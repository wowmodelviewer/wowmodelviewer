#include "dockWidget_Lighting.h"

dockWidgetLighting::dockWidgetLighting(QWidget *parent)
	: QWidget(parent)
{
	qInfo("Initializing Lighting Dock Widget...");
	ui.setupUi(this);
}