#pragma once

#include <string>
#include <vector>
#include "GL/glew.h"

class Model;
#include "Plugin.h"

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _EXPORTERPLUGIN_API_ __declspec(dllexport)
#    else
#        define _EXPORTERPLUGIN_API_ __declspec(dllimport)
#    endif
#else
#    define _EXPORTERPLUGIN_API_
#endif

class _EXPORTERPLUGIN_API_ ExporterPlugin : public Plugin
{
public:
	ExporterPlugin() : m_canExportAnimation(false)
	{
	}

	virtual std::wstring menuLabel() const = 0;
	virtual std::wstring fileSaveTitle() const = 0;
	virtual std::wstring fileSaveFilter() const = 0;

	virtual bool exportModel(Model*, std::wstring file) = 0;

	bool canExportAnimation() const { return m_canExportAnimation; }

	void setAnimationsToExport(std::vector<int> values) { m_animsToExport = values; }

protected:
	void exportGLTexture(GLuint id, std::wstring filename) const;
	bool m_canExportAnimation;
	std::vector<int> m_animsToExport;
};

#ifdef _EXPORTERPLUGIN_CPP_
Q_DECLARE_INTERFACE(ExporterPlugin, "wowmodelviewer.exporterplugin/1.0");
#endif
