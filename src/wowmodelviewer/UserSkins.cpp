#include "UserSkins.h"

#include <fstream>

#include "Game.h"
#include "logger/Logger.h"

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
		LOG_ERROR << "Failed to open '" << filename.c_str() << "' while loading user skins";
		return;
	}

	LOG_INFO << "Loading user skins from " << filename.c_str();
	// parse the file
	// See the comment at the end of this file for the exact format
	std::string line;
	size_t lineNr = 0;
	while (readline(in, line, lineNr, true)) {
		TextureSet set;
		wxString model = wxString(line.c_str(), wxConvUTF8);
		model.MakeLower();

		if (!readline(in, line, lineNr)) {
			LOG_ERROR << "UserSkins: unexpected EOF after '" << model.c_str() << "' (line" << lineNr << ")";
			return;
		}

		size_t numGroups = atoi(line.c_str());
		if (numGroups < 0) {
			LOG_ERROR << "UserSkins: negative number of groups specified in line" << lineNr;
			return;
		}
		if (numGroups > SET_WARN_COUNT) 
			LOG_INFO << "UserSkins: very large number of groups (" << numGroups << ") specified in line" << lineNr;
		
		for (size_t g=0; g < numGroups; ++g) {
			TextureGroup grp;
			int count = 0;
			for (size_t i=0; i < TextureGroup::num; ++i) {
				if (!readline(in, line, lineNr)) {
					LOG_ERROR << "UserSkins: unexpected EOF at line" << lineNr;
					return;
				}
				grp.tex[i] = GAMEDIRECTORY.getFile(line.c_str());
				if (grp.tex[i])
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
	LOG_INFO << "User skins" << filename.c_str() << "loaded";
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
