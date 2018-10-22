#include "dockWidget_Equipment.h"

dockWidgetEquipment::dockWidgetEquipment(QWidget *parent)
	: QWidget(parent)
{
	qInfo("Initializing Equipment Dock Widget...");
	ui.setupUi(this);
}