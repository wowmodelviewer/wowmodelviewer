#pragma once
#include "ui_dockWidget_FileList.h"

class dockWidgetFileList : public QWidget
{
	Q_OBJECT
public:
	dockWidgetFileList(QWidget *parent = 0);
	~dockWidgetFileList() {};

	void retranslate() { ui.retranslateUi(this); };

private:
	Ui::dockWidget_FileList ui;
};