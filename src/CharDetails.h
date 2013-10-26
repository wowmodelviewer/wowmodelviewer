/*
 * CharDetails.h
 *
 *  Created on: 26 oct. 2013
 *
 */

#ifndef _CHARDETAILS_H_
#define _CHARDETAILS_H_

#include "database.h"
#include "enums.h"

struct TabardDetails;

struct CharDetails
{
	size_t skinColor, faceType, hairColor, hairStyle, facialHair;
	size_t facialColor, maxFacialColor;
	size_t maxHairStyle, maxHairColor, maxSkinColor, maxFaceType, maxFacialHair;

	size_t race, gender;

	size_t useNPC;
	size_t eyeGlowType;

	bool showUnderwear, showEars, showHair, showFacialHair, showFeet;

	int equipment[NUM_CHAR_SLOTS];
	int geosets[NUM_GEOSETS];

	// save + load equipment
	void save(wxString fn, TabardDetails *td);
	bool load(wxString fn, TabardDetails *td);

	void loadSet(ItemSetDB &sets, ItemDatabase &items, int setid);
	void loadStart(StartOutfitDB &start, ItemDatabase &items, int cls);

	void reset();
};



#endif /* _CHARDETAILS_H_ */
