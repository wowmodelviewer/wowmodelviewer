#pragma once

#include <string>

class QCoreApplication;
class QThread;
class ModelViewer;

#include "Game.h"
#include "GlobalSettings.h"
#include "logger/Logger.h"
#include "metaclasses/Component.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _PLUGIN_API_ __declspec(dllexport)
#    else
#        define _PLUGIN_API_ __declspec(dllimport)
#    endif
#else
#    define _PLUGIN_API_
#endif

class _PLUGIN_API_ Plugin : public QObject, public Component
{
	Q_OBJECT

public:
	Plugin();
	~Plugin()
	{
	}

	// these fields are filled within json plugin informations and set by PluginManager
	// at load time
	std::string coreVersionNeeded() const { return m_coreVersionNeeded; }
	std::string version() const { return m_version; }
	std::string id() const { return (m_category + "_" + m_internalName); }

	static Plugin* load(std::string path, core::GlobalSettings&, core::Game&);

	// overload from component class
	void doPrint();

private:
	void transmitSingletonsFromCore(core::GlobalSettings&, core::Game&);

	std::string m_internalName;
	std::string m_category;
	std::string m_version;
	std::string m_coreVersionNeeded;

	static QCoreApplication* app;
	static QThread* thread;

	static core::GlobalSettings* globalSettings;
	static core::Game* game;

private slots:
	void onExec();
};
