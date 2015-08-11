#ifndef USERSKINS_H
#define USERSKINS_H

#include <cassert>
#include <string>
#include <map>

#include <wx/wx.h>

#include "animcontrol.h"

class UserSkins
{
	typedef std::map<wxString, TextureSet> TexSetMap;
	TexSetMap skins;
	bool loaded;
public:
	UserSkins()
	: loaded(false)
	{ }
	UserSkins(const wxString& filename);

	// Get the user defined skin for a model
	// The model must be all lowercase with an ".mdx" extension
	// If false is returned, set is not changed at all, otherwise
	// the user defined skins are added to it.
	bool AddUserSkins(const wxString& model, TextureSet& set);

	bool Loaded() const
	{
		return loaded;
	}

	void LoadFile(const wxString& filename);
};

#endif //USERSKINS_H
