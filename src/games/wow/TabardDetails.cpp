/*
 * TabardDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "TabardDetails.h"

#include "CASCFolder.h"
#include "wow_enums.h"



QString TabardDetails::GetBackgroundTex(int slot)
{
	if (slot == CR_TORSO_UPPER)
		return QString("Textures\\GuildEmblems\\Background_%1_TU_U.blp").arg(Background,2,10,QChar('0'));
	else
		return QString("Textures\\GuildEmblems\\Background_%1_TL_U.blp").arg(Background,2,10,QChar('0'));
}

QString TabardDetails::GetBorderTex(int slot)
{
	if (slot == CR_TORSO_UPPER)
		return QString("Textures\\GuildEmblems\\Border_%1_%2_TU_U.blp").arg(Border,2,10,QChar('0')).arg(BorderColor,2,10,QChar('0'));
	else
		return QString("Textures\\GuildEmblems\\Border_%1_%2_TL_U.blp").arg(Border,2,10,QChar('0')).arg(BorderColor,2,10,QChar('0'));
}

QString TabardDetails::GetIconTex(int slot)
{
	if(slot == CR_TORSO_UPPER)
		return QString("Textures\\GuildEmblems\\Emblem_%1_%2_TU_U.blp").arg(Icon,2,10,QChar('0')).arg(IconColor,2,10,QChar('0'));
	else
		return QString("Textures\\GuildEmblems\\Emblem_%1_%2_TL_U.blp").arg(Icon,2,10,QChar('0')).arg(IconColor,2,10,QChar('0'));
}

int TabardDetails::GetMaxBackground()
{
	int i = 0;
	while(CASCFOLDER.fileExists(QString("Textures\\GuildEmblems\\Background_%1_TU_U.blp").arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

int TabardDetails::GetMaxIcon()
{
	int i = 0;
	while(CASCFOLDER.fileExists(QString("Textures\\GuildEmblems\\Emblem_%1_00_TU_U.blp").arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

int TabardDetails::GetMaxIconColor(int icon)
{
	int i = 0;
	while(CASCFOLDER.fileExists(QString("Textures\\GuildEmblems\\Emblem_%1_%2_TU_U.blp").arg(icon,2,10,QChar('0')).arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

int TabardDetails::GetMaxBorder()
{
	int i = 0;
	while(CASCFOLDER.fileExists(QString("Textures\\GuildEmblems\\Border_%1_00_TU_U.blp").arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

int TabardDetails::GetMaxBorderColor(int border)
{
	int i = 0;
	while(CASCFOLDER.fileExists(QString("Textures\\GuildEmblems\\Border_%1_%2_TU_U.blp").arg(border,2,10,QChar('0')).arg(i,2,10,QChar('0')).toStdString()))
		i++;
	return i;
}

