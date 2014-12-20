/*
 * CharDetails.cpp
 *
 *  Created on: 26 oct. 2013
 *
 */

#include "CharDetails.h"

#include "TabardDetails.h"
#include "util.h" // correctType

#include <wx/wfstream.h>

void CharDetails::save(wxString fn, TabardDetails *td)
{
	// TODO: save/load as xml?
	// wx/xml/xml.h says the api will change, do not use etc etc.
	wxFFileOutputStream output( fn );
    wxTextOutputStream f( output );
	if (!output.IsOk())
		return;
	f << (int)race << wxT(" ") << (int)gender << endl;
	f << (int)skinColor << wxT(" ") << (int)faceType << wxT(" ") << (int)hairColor << wxT(" ") << (int)hairStyle << wxT(" ") << (int)facialHair << wxT(" ") << (int)facialColor << endl;
	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		f << equipment[i] << endl;
	}

	// 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
	if ((equipment[CS_TABARD] == 5976) || (equipment[CS_TABARD] == 69209) || (equipment[CS_TABARD] == 69210)) {
		f << td->Background << wxT(" ") << td->Border << wxT(" ") << td->BorderColor << wxT(" ") << td->Icon << wxT(" ") << td->IconColor << endl;
	}
	output.Close();
}

bool CharDetails::load(wxString fn, TabardDetails *td)
{
	unsigned int r, g;
	int tmp;
	bool same = false;

	// for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++)
			// equipment[i] = 0;

	wxFFileInputStream input( fn );
	if (!input.IsOk())
		return false;
	wxTextInputStream f( input );

	f >> r >> g;

	if (r==race && g==gender) {
#if defined _WINDOWS
		f >> skinColor >> faceType >> hairColor >> hairStyle >> facialHair >> facialColor;
#endif
		same = true;
	} else {
		int dummy;
		for (size_t i=0; i<6; i++) f >> dummy;
	}

	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		f >> tmp;

		//
		if (tmp > 0)
			equipment[i] = tmp;
	}

	// 5976 is the ID value for the Guild Tabard, 69209 for the Illustrious Guild Tabard, and 69210 for the Renowned Guild Tabard
	if (((equipment[CS_TABARD] == 5976) || (equipment[CS_TABARD] == 69209) || (equipment[CS_TABARD] == 69210)) && !input.Eof()) {
		f >> td->Background >> td->Border >> td->BorderColor >> td->Icon >> td->IconColor;
		td->showCustom = true;
	}

	//input.Close();
	return same;
}

void CharDetails::loadSet(ItemSetDB &sets, ItemDatabase &items, int setid)
{
	try {
		ItemSetDB::Record rec = sets.getById(setid);
		for (size_t i=0; i<ItemSetDB::NumItems; i++) {
			int id = rec.getInt(ItemSetDB::ItemIDBaseV400 + i);

			const ItemRecord &r = items.getById(id);
			if (r.type > 0) {
				// find a slot for it
				for (ssize_t s=0; s<NUM_CHAR_SLOTS; s++) {
					if (correctType((ssize_t)r.type, s)) {
						equipment[s] = id;
						break;
					}
				}
			}
		}
	} catch (ItemSetDB::NotFound) {}
}

void CharDetails::loadStart(StartOutfitDB &start, ItemDatabase &items, int setid)
{
	try {
		StartOutfitDB::Record rec = start.getById(setid);
		for (size_t i=0; i<StartOutfitDB::NumItems; i++) {
			int id = rec.getInt(StartOutfitDB::ItemIDBase + i);
			if (id==0) continue;
			const ItemRecord &r = items.getById(id);
			if (r.type > 0) {
				// find a slot for it
				for (ssize_t s=0; s<NUM_CHAR_SLOTS; s++) {
					if (correctType((ssize_t)r.type, s)) {
						equipment[s] = id;
						break;
					}
				}
			}
		}
	} catch (ItemSetDB::NotFound) {}
}

void CharDetails::reset()
{
	skinColor = 0;
	faceType = 0;
	hairColor = 0;
	hairStyle = 0;
	facialHair = 0;

	showUnderwear = true;
	showHair = true;
	showFacialHair = true;
	showEars = true;
	showFeet = false;

	for (ssize_t i=0; i<NUM_CHAR_SLOTS; i++) {
		equipment[i] = 0;
	}
}

void CharDetails::print()
{
	std::cout << "CharDetails::print()" << std::endl;
	std::cout << "skinColor = " << skinColor << std::endl;
	std::cout << "faceType = " << faceType << std::endl;
	std::cout << "hairColor = " << hairColor << std::endl;
	std::cout << "hairStyle = " << hairStyle << std::endl;
	std::cout << "facialHair = " << facialHair << std::endl;
	std::cout << "facialColor = " << facialColor << std::endl;
	std::cout << "maxFacialColor = " << maxFacialColor << std::endl;
	std::cout << "maxHairStyle = " << maxHairColor << std::endl;
	std::cout << "maxHairColor = " << maxHairColor << std::endl;
	std::cout << "maxSkinColor = " << maxSkinColor << std::endl;
	std::cout << "maxFaceType = " << maxFaceType << std::endl;
	std::cout << "maxFacialHair = " << maxFacialHair << std::endl;
	std::cout << "race = " << race << std::endl;
	std::cout << "gender = " << gender << std::endl;
	std::cout << "useNPC = " << useNPC << std::endl;
	std::cout << "eyeGlowType = " << eyeGlowType << std::endl;
	std::cout << "showUnderwear = " << showUnderwear << std::endl;
	std::cout << "showEars = " << showEars << std::endl;
	std::cout << "showHair = " << showHair << std::endl;
	std::cout << "showFacialHair = " << showFacialHair << std::endl;
	std::cout << "showFeet = " << showFeet << std::endl;
}
