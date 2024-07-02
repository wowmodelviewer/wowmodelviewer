/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * WowheadImporter.h
 *
 *  Created on: 1 dec. 2013
 *   Copyright: 2013 , WoW Model Viewer (http://wowmodelviewer.net)
 */

#pragma once

#include <QtPlugin>

#define _IMPORTERPLUGIN_CPP_ // to define interface
#include "ImporterPlugin.h"
#undef _IMPORTERPLUGIN_CPP_

class WowheadImporter : public ImporterPlugin
{
	Q_INTERFACES(ImporterPlugin)
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "wowmodelviewer.importers.WowheadImporter" FILE "wowheadimporter.json")

public:
	WowheadImporter()
	{
	}

	~WowheadImporter()
	{
	}

	bool acceptURL(QString url) const;

	NPCInfos* importNPC(QString url) const;
	CharInfos* importChar(QString url) const { return nullptr; }
	ItemRecord* importItem(QString url) const;

private:
	QString extractSubString(QString& datas, QString beginPattern, QString endPattern = QString()) const;
	QByteArray getURLData(QString inputUrl) const;
};
