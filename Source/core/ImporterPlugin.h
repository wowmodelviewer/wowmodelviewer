#pragma once

#include <QString>

struct ItemRecord;
class CharInfos;
class NPCInfos;

class ImporterPlugin
{
public:
	ImporterPlugin() = default;
	virtual ~ImporterPlugin() = default;

	virtual bool acceptURL(QString url) const = 0;
	virtual NPCInfos* importNPC(QString url) const = 0;
	virtual ItemRecord* importItem(QString url) const = 0;
	virtual CharInfos* importChar(QString url) const = 0;
};
