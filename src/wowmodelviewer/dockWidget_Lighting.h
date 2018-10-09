#pragma once
#include "ui_dockWidget_Lighting.h"

class dockWidgetLighting : public QWidget
{
	Q_OBJECT
public:
	dockWidgetLighting(QWidget *parent = 0);
	~dockWidgetLighting() {};

private:
	Ui::dockWidgetLighting ui;
};