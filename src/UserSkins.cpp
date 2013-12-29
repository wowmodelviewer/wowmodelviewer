#include <fstream>
#include "UserSkins.h"

#define SET_WARN_COUNT 10

UserSkins::UserSkins(const wxString &filename)
: loaded(false)
{
	LoadFile(filename);
}

// Comment and line number aware getline
static bool readline(std::istream& in, std::string& buf, size_t nr, bool skipEmpty = false)
{
	while (std::getline(in, buf)) {
		nr++;
		if (buf.empty() && skipEmpty)
			continue;
		if (!buf.empty() ? buf[0] != '#' : true)
			return true;
	}
	return false;
}

void UserSkins::LoadFile(const wxString &filename)
{
	if (!wxFileExists(filename)) {
		return;
	}

	std::ifstream in(filename.char_str());

	if (!in.is_open()) {
		wxLogMessage(wxT("Failed to open '%s' while loading user skins"), filename.c_str());
		return;
	}

	wxLogMessage(wxT("Loading user skins from '%s'"), filename.c_str());
	// parse the file
	// See the comment at the end of this file for the exact format
	std::string line;
	size_t lineNr = 0;
	while (readline(in, line, lineNr, true)) {
		TextureSet set;
		wxString model = wxString(line.c_str(), wxConvUTF8);
		model.MakeLower();

		if (!readline(in, line, lineNr)) {
			wxLogMessage(wxT("Error - UserSkins: unexpected EOF after '%s' (line %d)"), model.c_str(), lineNr);
			return;
		}

		size_t numGroups = atoi(line.c_str());
		if (numGroups < 0) {
			wxLogMessage(wxT("Error - UserSkins: negativ number of groups specified in line %d"), lineNr);
			return;
		}
		if (numGroups > SET_WARN_COUNT) 
			wxLogMessage(wxT("Warning - UserSkins: very large number of groups (%d) specified in line %d"), numGroups, lineNr);
		
		for (size_t g=0; g < numGroups; ++g) {
			TextureGroup grp;
			int count = 0;
			for (size_t i=0; i < TextureGroup::num; ++i) {
				if (!readline(in, line, lineNr)) {
					wxLogMessage(wxT("Error - UserSkins: unexpected EOF at line %d"), lineNr);
					return;
				}
				grp.tex[i] = wxString(line.c_str(), wxConvUTF8);
				if (grp.tex[i] != wxEmptyString)
					count++;
			}
			grp.base = TEXTURE_GAMEOBJECT1;
			grp.count = count;
			set.insert(grp);
		}
		skins.insert(std::make_pair(model, set));
	}
	// everything ok (-:
	loaded = true;
	wxLogMessage(wxT("User skins %s loaded"), filename.c_str());
	in.close();
}

bool UserSkins::AddUserSkins(const wxString &model, TextureSet &set)
{
	if (!loaded)
		return false;

	TexSetMap::iterator it = skins.find(model);
	if (it == skins.end())
		return false;

	TextureSet& myset = it->second;
	for (TextureSet::iterator i=myset.begin(); i != myset.end(); ++i)
		set.insert(*i);
	return true;
}

/**
	UserSkins file format:

	path\to\modelname.mdx
	<number of sets>
	<for each set: 3 lines, 1 texture per line (or blank line)>
	<any number of empty lines are allowed after all sets>
	Lines that begin with an '#' are ignored.

	E.g:
--
Creature\Dragon\DragonSkywallMount.mdx
2
DragonSkywallMountGreen1
DragonSkywallMountGreen2
DragonSkywallMountGreenSaddle
DragonSkywallMountPale1
DragonSkywallMountPale2
DragonSkywallMountPaleSaddle

Creature\Camel\CamelMount.mdx
2
CamelMount_brown
CamelSaddle_brown

CamelMount_grey
CamelSaddle_grey

--
**/
