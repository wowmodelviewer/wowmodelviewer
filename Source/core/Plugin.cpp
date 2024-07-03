#include "Plugin.h"
#include <iostream>
#include <QCoreApplication>
#include <QPluginLoader>
#include <QThread>

core::GlobalSettings* Plugin::globalSettings = nullptr;
core::Game* Plugin::game = nullptr;

QCoreApplication* Plugin::app = nullptr;
QThread* Plugin::thread = nullptr;

Plugin::Plugin() : Component(), m_internalName(""), m_category(""), m_version(""), m_coreVersionNeeded("")
{
}

// Private Qt application
Plugin* Plugin::load(std::string path, core::GlobalSettings& settings, core::Game& Game)
{
	const QString pluginToLoad = QString::fromStdString(path);
	Plugin* newPlugin = nullptr;

	QPluginLoader loader(pluginToLoad);

	if (QObject* plugin = loader.instance())
	{
		newPlugin = dynamic_cast<Plugin*>(plugin);
		const QJsonObject metaInfos = loader.metaData().value("MetaData").toObject();
		newPlugin->setName(metaInfos.value("name").toString());
		newPlugin->m_version = metaInfos.value("version").toString().toStdString();
		newPlugin->m_coreVersionNeeded = metaInfos.value("coreVersion").toString().toStdString();
		newPlugin->m_internalName = metaInfos.value("internalname").toString().toStdString();
		newPlugin->m_category = metaInfos.value("category").toString().toStdString();

		newPlugin->transmitSingletonsFromCore(settings, Game);

		// waiting for the overall application being a Qt application, we start a QCoreApplication in a dedicated
		// thread for each plugin, so that Qt event loop is accessible from plugins (see onExec slot that actually
		// starts the app)
		Plugin::thread = new QThread();
		connect(Plugin::thread, SIGNAL(started()), newPlugin, SLOT(onExec()), Qt::DirectConnection);
		Plugin::thread->start();

		return newPlugin;
	}
	else
	{
		std::cout << "Unable to load plugin file " << path << ": " << loader.errorString().toStdString() << std::endl;
	}
	return newPlugin;
}

void Plugin::doPrint()
{
	std::cout << id() << ": " << name().toStdString() << " (version: " << m_version << " - core needed: " <<
		m_coreVersionNeeded << ")" << std::endl;
}

void Plugin::transmitSingletonsFromCore(core::GlobalSettings& settings, core::Game& Game)
{
	Plugin::globalSettings = &settings;
	Plugin::game = &Game;
}

void Plugin::onExec()
{
	if (QCoreApplication::instance() == nullptr)
	{
		int argc = 1;
		const char* argv[] = {"plugin.app", nullptr};
		app = new QCoreApplication(argc, const_cast<char**>(argv));
		app->exec();
		if (app)
			delete app;
	}
}
