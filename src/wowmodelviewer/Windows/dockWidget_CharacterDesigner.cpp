#include "dockWidget_CharacterDesigner.h"

dockWidgetCharacterDesigner::dockWidgetCharacterDesigner(QWidget *parent)
	: QWidget(parent)
{
	qInfo("Initializing Character Designer Dock Widget...");
	ui.setupUi(this);

	// Hide what we initially don't need
	setFeatureWidgetVisible(CHARFEATURE_PIERCINGS, false);
	setFeatureWidgetVisible(CHARFEATURE_FEATURES, false);
	setFeatureWidgetVisible(CHARFEATURE_TUSKS, false);
	setFeatureWidgetVisible(CHARFEATURE_EARS, false);
	setFeatureWidgetVisible(CHARFEATURE_HORNS, false);
	setFeatureWidgetVisible(CHARFEATURE_HORNSCOLOR, false);
	setFeatureWidgetVisible(CHARFEATURE_TATTOO, false);
	setFeatureWidgetVisible(CHARFEATURE_TATTOOCOLOR, false);
	setFeatureWidgetVisible(CHARFEATURE_BLINDFOLD, false);
	setFeatureWidgetVisible(CHARFEATURE_DEMONHUNTER, false);

	// Make connections
	connect(ui.charRandomizeButton, &QAbstractButton::pressed, this, &dockWidgetCharacterDesigner::randomizeAppearance);

	// Set default values for our features
	for (int i = 0; i < CHARFEATURE_MAX; i++)
	{
		setFeatureValue(static_cast<CharacterFeatures>(i), 0, 0);
	}
}

int dockWidgetCharacterDesigner::getFeatureValue(CharacterFeatures feature)
{
	switch (feature)
	{
	case CHARFEATURE_SKIN:
		return ui.spinBoxSkin->value();
		break;
	case CHARFEATURE_FACE:
		return ui.spinBoxFace->value();
		break;
	case CHARFEATURE_HAIRSTYLE:
		return ui.spinBoxHairStyle->value();
		break;
	case CHARFEATURE_HAIRCOLOR:
		return ui.spinBoxHairColor->value();
		break;
	case CHARFEATURE_PIERCINGS:
		return ui.spinBoxPiercings->value();
		break;
	case CHARFEATURE_FEATURES:
		return ui.spinBoxFeatures->value();
		break;
	case CHARFEATURE_TUSKS:
		return ui.spinBoxTusks->value();
		break;
	case CHARFEATURE_FACIALHAIR:
		return ui.spinBoxFacialHair->value();
		break;
	case CHARFEATURE_EARS:
		return ui.spinBoxEars->value();
		break;
	case CHARFEATURE_HORNS:
		return ui.spinBoxHorns->value();
		break;
	case CHARFEATURE_HORNSCOLOR:
		return ui.spinBoxHornsColor->value();
		break;
	case CHARFEATURE_TATTOO:
		return ui.spinBoxTattoo->value();
		break;
	case CHARFEATURE_TATTOOCOLOR:
		return ui.spinBoxTattooColor->value();
		break;
	case CHARFEATURE_BLINDFOLD:
		return ui.spinBoxBlindfold->value();
		break;
	case CHARFEATURE_DEMONHUNTER:
	default:
		break;
	}

	return 0;
}

int dockWidgetCharacterDesigner::getFeatureMaxValue(CharacterFeatures feature)
{
	switch (feature)
	{
	case CHARFEATURE_SKIN:
		return ui.labelMaxSkin->text().toInt();
		break;
	case CHARFEATURE_FACE:
		return ui.labelMaxFace->text().toInt();
		break;
	case CHARFEATURE_HAIRSTYLE:
		return ui.labelMaxHairStyle->text().toInt();
		break;
	case CHARFEATURE_HAIRCOLOR:
		return ui.labelMaxHaircolor->text().toInt();
		break;
	case CHARFEATURE_PIERCINGS:
		return ui.labelMaxPiercings->text().toInt();
		break;
	case CHARFEATURE_FEATURES:
		return ui.labelMaxFeatures->text().toInt();
		break;
	case CHARFEATURE_TUSKS:
		return ui.labelMaxTusks->text().toInt();
		break;
	case CHARFEATURE_FACIALHAIR:
		return ui.labelMaxFacialHair->text().toInt();
		break;
	case CHARFEATURE_EARS:
		return ui.labelMaxEars->text().toInt();
		break;
	case CHARFEATURE_HORNS:
		return ui.labelMaxHorns->text().toInt();
		break;
	case CHARFEATURE_HORNSCOLOR:
		return ui.labelMaxHornsColor->text().toInt();
		break;
	case CHARFEATURE_TATTOO:
		return ui.labelMaxTattoo->text().toInt();
		break;
	case CHARFEATURE_TATTOOCOLOR:
		return ui.labelMaxTattooColor->text().toInt();
		break;
	case CHARFEATURE_BLINDFOLD:
		return ui.labelMaxBlindford->text().toInt();
		break;
	case CHARFEATURE_DEMONHUNTER:
	default:
		break;
	}

	return 0;
}

void dockWidgetCharacterDesigner::setFeatureWidgetEnabled(CharacterFeatures feature, bool enabled)
{
	switch (feature)
	{
	case CHARFEATURE_SKIN:
		ui.labelSkin->setEnabled(enabled);
		ui.spinBoxSkin->setEnabled(enabled);
		ui.labelMaxSkin->setEnabled(enabled);
		break;
	case CHARFEATURE_FACE:
		ui.labelFace->setEnabled(enabled);
		ui.spinBoxFace->setEnabled(enabled);
		ui.labelMaxFace->setEnabled(enabled);
		break;
	case CHARFEATURE_HAIRSTYLE:
		ui.labelHairStyle->setEnabled(enabled);
		ui.spinBoxHairStyle->setEnabled(enabled);
		ui.labelMaxHairStyle->setEnabled(enabled);
		break;
	case CHARFEATURE_HAIRCOLOR:
		ui.labelHairColor->setEnabled(enabled);
		ui.spinBoxHairColor->setEnabled(enabled);
		ui.labelMaxHaircolor->setEnabled(enabled);
		break;
	case CHARFEATURE_PIERCINGS:
		ui.labelPiercings->setEnabled(enabled);
		ui.spinBoxPiercings->setEnabled(enabled);
		ui.labelMaxPiercings->setEnabled(enabled);
		break;
	case CHARFEATURE_FEATURES:
		ui.labelFeatures->setEnabled(enabled);
		ui.spinBoxFeatures->setEnabled(enabled);
		ui.labelMaxFeatures->setEnabled(enabled);
		break;
	case CHARFEATURE_TUSKS:
		ui.labelTusks->setEnabled(enabled);
		ui.spinBoxTusks->setEnabled(enabled);
		ui.labelMaxTusks->setEnabled(enabled);
		break;
	case CHARFEATURE_FACIALHAIR:
		ui.labelFacialHair->setEnabled(enabled);
		ui.spinBoxFacialHair->setEnabled(enabled);
		ui.labelMaxFacialHair->setEnabled(enabled);
		break;
	case CHARFEATURE_EARS:
		ui.labelEars->setEnabled(enabled);
		ui.spinBoxEars->setEnabled(enabled);
		ui.labelMaxEars->setEnabled(enabled);
		break;
	case CHARFEATURE_HORNS:
		ui.labelHorns->setEnabled(enabled);
		ui.spinBoxHorns->setEnabled(enabled);
		ui.labelMaxHorns->setEnabled(enabled);
		break;
	case CHARFEATURE_HORNSCOLOR:
		ui.labelHornsColor->setEnabled(enabled);
		ui.spinBoxHornsColor->setEnabled(enabled);
		ui.labelMaxHornsColor->setEnabled(enabled);
		break;
	case CHARFEATURE_TATTOO:
		ui.labelTattoo->setEnabled(enabled);
		ui.spinBoxTattoo->setEnabled(enabled);
		ui.labelMaxTattoo->setEnabled(enabled);
		break;
	case CHARFEATURE_TATTOOCOLOR:
		ui.labelTattooColor->setEnabled(enabled);
		ui.spinBoxTattooColor->setEnabled(enabled);
		ui.labelMaxTattooColor->setEnabled(enabled);
		break;
	case CHARFEATURE_BLINDFOLD:
		ui.labelBlindfold->setEnabled(enabled);
		ui.spinBoxBlindfold->setEnabled(enabled);
		ui.labelMaxBlindford->setEnabled(enabled);
		break;
	case CHARFEATURE_DEMONHUNTER:
		ui.checkBoxDemonHunter->setEnabled(enabled);
		ui.labelHorns->setEnabled(enabled);
		ui.spinBoxHorns->setEnabled(enabled);
		ui.labelMaxHorns->setEnabled(enabled);
		ui.labelBlindfold->setEnabled(enabled);
		ui.spinBoxBlindfold->setEnabled(enabled);
		ui.labelMaxBlindford->setEnabled(enabled);
		break;
	default:
		break;
	}
}

void dockWidgetCharacterDesigner::setFeatureWidgetVisible(CharacterFeatures feature, bool visible)
{
	switch (feature)
	{
	case CHARFEATURE_SKIN:
		ui.labelSkin->setVisible(visible);
		ui.spinBoxSkin->setVisible(visible);
		ui.labelMaxSkin->setVisible(visible);
		break;
	case CHARFEATURE_FACE:
		ui.labelFace->setVisible(visible);
		ui.spinBoxFace->setVisible(visible);
		ui.labelMaxFace->setVisible(visible);
		break;
	case CHARFEATURE_HAIRSTYLE:
		ui.labelHairStyle->setVisible(visible);
		ui.spinBoxHairStyle->setVisible(visible);
		ui.labelMaxHairStyle->setVisible(visible);
		break;
	case CHARFEATURE_HAIRCOLOR:
		ui.labelHairColor->setVisible(visible);
		ui.spinBoxHairColor->setVisible(visible);
		ui.labelMaxHaircolor->setVisible(visible);
		break;
	case CHARFEATURE_PIERCINGS:
		ui.labelPiercings->setVisible(visible);
		ui.spinBoxPiercings->setVisible(visible);
		ui.labelMaxPiercings->setVisible(visible);
		break;
	case CHARFEATURE_FEATURES:
		ui.labelFeatures->setVisible(visible);
		ui.spinBoxFeatures->setVisible(visible);
		ui.labelMaxFeatures->setVisible(visible);
		break;
	case CHARFEATURE_TUSKS:
		ui.labelTusks->setVisible(visible);
		ui.spinBoxTusks->setVisible(visible);
		ui.labelMaxTusks->setVisible(visible);
		break;
	case CHARFEATURE_FACIALHAIR:
		ui.labelFacialHair->setVisible(visible);
		ui.spinBoxFacialHair->setVisible(visible);
		ui.labelMaxFacialHair->setVisible(visible);
		break;
	case CHARFEATURE_EARS:
		ui.labelEars->setVisible(visible);
		ui.spinBoxEars->setVisible(visible);
		ui.labelMaxEars->setVisible(visible);
		break;
	case CHARFEATURE_HORNS:
		ui.labelHorns->setVisible(visible);
		ui.spinBoxHorns->setVisible(visible);
		ui.labelMaxHorns->setVisible(visible);
		break;
	case CHARFEATURE_HORNSCOLOR:
		ui.labelHornsColor->setVisible(visible);
		ui.spinBoxHornsColor->setVisible(visible);
		ui.labelMaxHornsColor->setVisible(visible);
		break;
	case CHARFEATURE_TATTOO:
		ui.labelTattoo->setVisible(visible);
		ui.spinBoxTattoo->setVisible(visible);
		ui.labelMaxTattoo->setVisible(visible);
		break;
	case CHARFEATURE_TATTOOCOLOR:
		ui.labelTattooColor->setVisible(visible);
		ui.spinBoxTattooColor->setVisible(visible);
		ui.labelMaxTattooColor->setVisible(visible);
		break;
	case CHARFEATURE_BLINDFOLD:
		ui.labelBlindfold->setVisible(visible);
		ui.spinBoxBlindfold->setVisible(visible);
		ui.labelMaxBlindford->setVisible(visible);
		break;
	case CHARFEATURE_DEMONHUNTER:
		ui.checkBoxDemonHunter->setVisible(visible);
		ui.labelHorns->setVisible(visible);
		ui.spinBoxHorns->setVisible(visible);
		ui.labelMaxHorns->setVisible(visible);
		ui.labelBlindfold->setVisible(visible);
		ui.spinBoxBlindfold->setVisible(visible);
		ui.labelMaxBlindford->setVisible(visible);
		break;
	default:
		break;
	}
}

void dockWidgetCharacterDesigner::setFeatureValue(CharacterFeatures feature, int current, int max)
{
	switch (feature)
	{
	case CHARFEATURE_SKIN:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxSkin->setValue(current);
		if (max > -1)
			ui.labelMaxSkin->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_FACE:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxFace->setValue(current);
		if (max > -1)
			ui.labelMaxFace->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_HAIRSTYLE:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxHairStyle->setValue(current);
		if (max > -1)
			ui.labelMaxHairStyle->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_HAIRCOLOR:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxHairColor->setValue(current);
		if (max > -1)
			ui.labelMaxHaircolor->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_PIERCINGS:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxPiercings->setValue(current);
		if (max > -1)
			ui.labelMaxPiercings->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_FEATURES:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxFeatures->setValue(current);
		if (max > -1)
			ui.labelMaxFeatures->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_TUSKS:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxTusks->setValue(current);
		if (max > -1)
			ui.labelMaxTusks->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_FACIALHAIR:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxFacialHair->setValue(current);
		if (max > -1)
			ui.labelMaxFacialHair->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_EARS:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxEars->setValue(current);
		if (max > -1)
			ui.labelMaxEars->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_HORNS:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxHorns->setValue(current);
		if (max > -1)
			ui.labelMaxHorns->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_HORNSCOLOR:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxHornsColor->setValue(current);
		if (max > -1)
			ui.labelMaxHornsColor->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_TATTOO:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxTattoo->setValue(current);
		if (max > -1)
			ui.labelMaxTattoo->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_TATTOOCOLOR:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxTattooColor->setValue(current);
		if (max > -1)
			ui.labelMaxTattooColor->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_BLINDFOLD:
		setFeatureWidgetEnabled(feature, true);
		ui.spinBoxBlindfold->setValue(current);
		if (max > -1)
			ui.labelMaxBlindford->setText(QString::number(max));
		if (max == 0) {
			setFeatureWidgetEnabled(feature, false);
		}
		break;
	case CHARFEATURE_DEMONHUNTER:
	default:
		break;
	}
}

void dockWidgetCharacterDesigner::randomizeAppearance()
{
	qInfo("Randomizing appearance!");
	for (int i = 0; i < CHARFEATURE_MAX; i++)
	{
		CharacterFeatures f = static_cast<CharacterFeatures>(i);
		int max = getFeatureMaxValue(f);
		if (max == 0) continue;

		srand(time(NULL));
		int randomchoice = rand() % max;
		setFeatureValue(f, randomchoice);
	}
}
