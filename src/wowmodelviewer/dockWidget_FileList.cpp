#include "dockWidget_FileList.h"

dockWidgetFileList::dockWidgetFileList(QWidget *parent)
	: QWidget(parent)
{
	qInfo("Initializing File List Dock Widget...");
	ui.setupUi(this);
}