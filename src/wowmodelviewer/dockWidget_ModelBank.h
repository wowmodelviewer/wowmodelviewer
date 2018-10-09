#pragma once
#include "ui_dockWidget_ModelBank.h"

class dockWidgetModelBank : public QWidget
{
	Q_OBJECT
public:
	dockWidgetModelBank(QWidget *parent = 0);
	~dockWidgetModelBank() {};

private:
	Ui::dockWidget_ModelBank ui;
};