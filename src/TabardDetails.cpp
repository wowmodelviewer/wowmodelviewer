/*
 * TabardDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "TabardDetails.h"

#include "enums.h"
#include "CASCFile.h"
#include "CASCFolder.h"



wxString TabardDetails::GetBackgroundTex(int slot)
{
	if (slot == CR_TORSO_UPPER)
		return wxString::Format(wxT("Textures\\GuildEmblems\\Background_%02d_TU_U.blp"), Background);
	else
		return wxString::Format(wxT("Textures\\GuildEmblems\\Background_%02d_TL_U.blp"), Background);
}

wxString TabardDetails::GetBorderTex(int slot)
{
	if (slot == CR_TORSO_UPPER)
		return wxString::Format(wxT("Textures\\GuildEmblems\\Border_%02d_%02d_TU_U.blp"), Border, BorderColor);
	else
		return wxString::Format(wxT("Textures\\GuildEmblems\\Border_%02d_%02d_TL_U.blp"), Border, BorderColor);
}

wxString TabardDetails::GetIconTex(int slot)
{
	if(slot == CR_TORSO_UPPER)
		return wxString::Format(wxT("Textures\\GuildEmblems\\Emblem_%02d_%02d_TU_U.blp"), Icon, IconColor);
	else
		return wxString::Format(wxT("Textures\\GuildEmblems\\Emblem_%02d_%02d_TL_U.blp"), Icon, IconColor);
}

int TabardDetails::GetMaxBackground()
{
	int i = 0;
	CASCFile file;
	while(1)
	{
	  file.openFile(std::string(wxString::Format(wxT("Textures\\GuildEmblems\\Background_%02d_TU_U.blp"), i).mb_str()));
	  if(file.isEof())
			break;
	  file.close();
		i ++;
	}
	return i;
}

int TabardDetails::GetMaxIcon()
{
	int i = 0;
	CASCFile file;
	while(1)
	{
	  file.openFile(std::string(wxString::Format(wxT("Textures\\GuildEmblems\\Emblem_%02d_00_TU_U.blp"), i).mb_str()));
	  if(file.isEof())
			break;
	  file.close();
		i ++;
	}
	return i;
}

int TabardDetails::GetMaxIconColor(int icon)
{
	int i = 0;
	CASCFile file;
	while(1)
	{
	  file.openFile(std::string(wxString::Format(wxT("Textures\\GuildEmblems\\Emblem_%02d_%02d_TU_U.blp"), icon, i).mb_str()));
	  if(file.isEof())
			break;
	  file.close();
		i ++;
	}
	return i;
}

int TabardDetails::GetMaxBorder()
{
	int i = 0;
	CASCFile file;
	while(1)
	{
	  file.openFile(std::string(wxString::Format(wxT("Textures\\GuildEmblems\\Border_%02d_00_TU_U.blp"), i).mb_str()));
	  if(file.isEof())
			break;
	  file.close();
		i ++;
	}
	return i;
}

int TabardDetails::GetMaxBorderColor(int border)
{
	int i = 0;
	CASCFile file;
	while(1)
	{
	  file.openFile(std::string(wxString::Format(wxT("Textures\\GuildEmblems\\Border_%02d_%02d_TU_U.blp"), border, i).mb_str()));
		if (CASCFOLDER.getFullPathForFile(wxString::Format(wxT("Textures\\GuildEmblems\\Border_%02d_%02d_TU_U.blp"), border, i)) == "")
			break;
		file.close();
		i ++;
	}
	return i;
}

