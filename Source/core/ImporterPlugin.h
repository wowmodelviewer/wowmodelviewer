#pragma once

struct ItemRecord;
class CharInfos;
class NPCInfos;

#include "Plugin.h"

class ImporterPlugin : public Plugin
{
public:
	ImporterPlugin()
	{
	}

	virtual bool acceptURL(QString url) const = 0;
	virtual NPCInfos* importNPC(QString url) const = 0;
	virtual ItemRecord* importItem(QString url) const = 0;
	virtual CharInfos* importChar(QString url) const = 0;
};

#ifdef _IMPORTERPLUGIN_CPP_
Q_DECLARE_INTERFACE(ImporterPlugin, "wowmodelviewer.importerplugin/1.0");
#endif
