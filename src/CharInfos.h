/*
 * CharInfos.h
 *
 *  Created on: 26 nov. 2013
 *
 */

#ifndef _CHARINFOS_H_
#define _CHARINFOS_H_

#include "CharDetails.h"

#include <wx/string.h>

class CharInfos
{
	public:
	size_t raceId;
	size_t genderId;
	wxString race;
	wxString gender;
	bool hasTransmogGear;
	CharDetails cd;
};


#endif /* _CHARINFOS_H_ */
