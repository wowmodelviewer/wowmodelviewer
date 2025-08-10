/*
 * FileTreeItem.h
 *
 *  Created on: 20 déc. 2014
 *      Author: Jerome
 */

#pragma once

#include <QString>

class FileTreeItem
{
public:
	QString displayName;

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
