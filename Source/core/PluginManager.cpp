#include "PluginManager.h"
#include <iostream>
#include <QDir>
#include <QString>
#include <QStringList>
#include "Game.h"
#include "GlobalSettings.h"

PluginManager* PluginManager::m_instance = nullptr;

PluginManager::PluginManager()
{
}

void PluginManager::init(const std::string& dir)
{
	QString directory = QString::fromStdString(dir);
	QDir pluginDir(directory);
	QStringList plugins = pluginDir.entryList(QDir::Files);
	for (int i = 0; i < plugins.size(); i++)
	{
		if (plugins.at(i).contains(".dll") == false) continue; // Skip non-plugin files
		Plugin* newPlugin = Plugin::load(pluginDir.absoluteFilePath(plugins[(int)i]).toStdString(),GLOBALSETTINGS,
		                                 core::Game::instance());
		if (newPlugin)
			addChild(newPlugin);
	}
	print();
}

void PluginManager::doPrint()
{
	std::cout << "PluginManager (" << nbChildren() << " plugins loaded)" << std::endl;
}
