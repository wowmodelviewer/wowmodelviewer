#pragma once
#include "ui_dockWidget_Equipment.h"

class dockWidgetEquipment : public QWidget
{
	Q_OBJECT
public:
	dockWidgetEquipment(QWidget *parent = 0);
	~dockWidgetEquipment() {};

	void retranslate() { ui.retranslateUi(this); };

private:
	Ui::dockWidget_Equipment ui;
};