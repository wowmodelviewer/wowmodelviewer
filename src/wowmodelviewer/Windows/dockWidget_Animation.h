#pragma once
#include "ui_dockWidget_Animation.h"

class dockWidgetAnimation : public QWidget
{
	Q_OBJECT
public:
	dockWidgetAnimation(QWidget *parent = 0);
	~dockWidgetAnimation() {};

	void retranslate() { ui.retranslateUi(this); };

private:
	Ui::dockWidget_AnimationControls ui;
};