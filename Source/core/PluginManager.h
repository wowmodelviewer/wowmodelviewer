#pragma once

#include "metaclasses/Container.h"
#include "Plugin.h"

#define PLUGINMANAGER PluginManager::instance()

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _PLUGINMANAGER_API_ __declspec(dllexport)
#    else
#        define _PLUGINMANAGER_API_ __declspec(dllimport)
#    endif
#else
#    define _PLUGINMANAGER_API_
#endif

class _PLUGINMANAGER_API_ PluginManager : public Container<Plugin>
{
public:
	static PluginManager& instance()
	{
		if (PluginManager::m_instance == nullptr)
			PluginManager::m_instance = new PluginManager();

		return *m_instance;
	}

	// load all plugins in given directory
	void init(const std::string& pluginDir);

	// overloaded from Component method
	void doPrint();

private:
	PluginManager();
	PluginManager(PluginManager&);

	static PluginManager* m_instance;
};
