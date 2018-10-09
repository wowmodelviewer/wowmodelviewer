#include "dockWidget_CharacterDesigner.h"

dockWidgetCharacterDesigner::dockWidgetCharacterDesigner(QWidget *parent)
	: QWidget(parent)
{
	qInfo("Initializing Character Designer Dock Widget...");
	ui.setupUi(this);
}