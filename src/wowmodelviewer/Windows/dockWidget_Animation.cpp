#include "dockWidget_Animation.h"

dockWidgetAnimation::dockWidgetAnimation(QWidget *parent)
	: QWidget(parent)
{
	qInfo("Initializing Animation Dock Widget...");
	ui.setupUi(this);
}