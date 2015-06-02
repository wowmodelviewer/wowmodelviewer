/*
 * TabardDetails.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#ifndef _TABARDDETAILS_H_
#define _TABARDDETAILS_H_

#include <wx/string.h>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _TABARDDETAILS_API_ __declspec(dllexport)
#    else
#        define _TABARDDETAILS_API_ __declspec(dllimport)
#    endif
#else
#    define _TABARDDETAILS_API_
#endif

struct _TABARDDETAILS_API_ TabardDetails
{
	int Icon;
	int IconColor;
	int Border;
	int BorderColor;
	int Background;

	int maxIcon;
	int maxIconColor;
	int maxBorder;
	int maxBorderColor;
	int maxBackground;

	bool showCustom;

	wxString GetIconTex(int slot);
	wxString GetBorderTex(int slot);
	wxString GetBackgroundTex(int slot);

	int GetMaxIcon();
	int GetMaxIconColor(int icon);
	int GetMaxBorder();
	int GetMaxBorderColor(int border);
	int GetMaxBackground();
};


#endif /* _TABARDDETAILS_H_ */
