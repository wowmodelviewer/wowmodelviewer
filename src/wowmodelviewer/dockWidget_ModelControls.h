#pragma once
#include "ui_dockWidget_ModelControls.h"

class dockWidgetModelControls : public QWidget
{
	Q_OBJECT
public:
	dockWidgetModelControls(QWidget *parent = 0);
	~dockWidgetModelControls() {};

private:
	Ui::dockWidgetModelControls ui;
};