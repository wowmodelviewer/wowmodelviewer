#include "dialog_Settings.h"

dialogSettings::dialogSettings(QWidget *parent)
	: QDialog(parent)
{
	qInfo("Initializing Settings Dialog...");
	ui.setupUi(this);
}