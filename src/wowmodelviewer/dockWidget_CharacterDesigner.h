#pragma once
#include "ui_dockWidget_CharacterDesigner.h"

enum CharacterFeatures
{
	CHARFEATURE_SKIN = 0,
	CHARFEATURE_FACE,
	CHARFEATURE_HAIRSTYLE,
	CHARFEATURE_HAIRCOLOR,
	CHARFEATURE_PIERCINGS,
	CHARFEATURE_FEATURES,
	CHARFEATURE_TUSKS,
	CHARFEATURE_FACIALHAIR,
	CHARFEATURE_EARS,
	CHARFEATURE_HORNS,
	CHARFEATURE_HORNSCOLOR,
	CHARFEATURE_TATTOO,
	CHARFEATURE_TATTOOCOLOR,
	CHARFEATURE_BLINDFOLD,
	CHARFEATURE_DEMONHUNTER,

	CHARFEATURE_MAX
};

class dockWidgetCharacterDesigner : public QWidget
{
	Q_OBJECT
public:
	dockWidgetCharacterDesigner(QWidget *parent = 0);
	~dockWidgetCharacterDesigner() {};

	int getFeatureValue(CharacterFeatures feature);
	int getFeatureMaxValue(CharacterFeatures feature);
	bool getIsDemonHunter() { return ui.checkBoxDemonHunter->isChecked(); };
	void setFeatureWidgetEnabled(CharacterFeatures feature, bool enabled);
	void setFeatureWidgetVisible(CharacterFeatures feature, bool visible);
	void setFeatureValue(CharacterFeatures feature, int current = 0, int max = -1);
	void setIsDemonHunter(bool value) { ui.checkBoxDemonHunter->setChecked(value); };

	void retranslate() { ui.retranslateUi(this); };

public slots:
	void randomizeAppearance();

private:
	Ui::dockWidget_CharacterDesigner ui;
};