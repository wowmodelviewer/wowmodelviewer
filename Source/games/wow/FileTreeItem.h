/*
 * FileTreeItem.h
 *
 *  Created on: 20 déc. 2014
 *      Author: Jerome
 */

#pragma once

#include <string>

class FileTreeItem
{
public:
	std::string displayName;

	int color;

	/// Comparison
	bool operator<(const FileTreeItem& i) const
	{
		return displayName < i.displayName;
	}

	bool operator>(const FileTreeItem& i) const
	{
		return displayName > i.displayName;
	}
};
