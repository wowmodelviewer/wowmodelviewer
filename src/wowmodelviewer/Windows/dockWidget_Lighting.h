#pragma once
#include "ui_dockWidget_Lighting.h"

class dockWidgetLighting : public QWidget
{
	Q_OBJECT
public:
	dockWidgetLighting(QWidget *parent = 0);
	~dockWidgetLighting() {};

	void retranslate() { ui.retranslateUi(this); };

private:
	Ui::dockWidgetLighting ui;
};