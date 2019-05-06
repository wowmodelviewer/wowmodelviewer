#pragma once
#include "ui_dialog_Settings.h"

class dialogSettings : public QDialog
{
	Q_OBJECT
public:
	dialogSettings(QWidget *parent = 0);
	~dialogSettings() {};

	void retranslate() { ui.retranslateUi(this); };

private:
	Ui::dialog_Settings ui;
};