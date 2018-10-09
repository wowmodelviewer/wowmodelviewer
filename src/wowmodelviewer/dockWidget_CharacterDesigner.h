#pragma once
#include "ui_dockWidget_CharacterDesigner.h"

class dockWidgetCharacterDesigner : public QWidget
{
	Q_OBJECT
public:
	dockWidgetCharacterDesigner(QWidget *parent = 0);
	~dockWidgetCharacterDesigner() {};

private:
	Ui::dockWidget_CharacterDesigner ui;
};