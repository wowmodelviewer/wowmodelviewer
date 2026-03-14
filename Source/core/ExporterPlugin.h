#pragma once

#include <string>
#include <vector>
#include "GL/glew.h"

#include "logger/Logger.h"

class Model;

#define _EXPORTERPLUGIN_API_

class _EXPORTERPLUGIN_API_ ExporterPlugin
{
public:
	ExporterPlugin() = default;
	virtual ~ExporterPlugin() = default;

	virtual std::wstring menuLabel() const = 0;
	virtual std::wstring fileSaveTitle() const = 0;
	virtual std::wstring fileSaveFilter() const = 0;

	virtual bool exportModel(Model*, std::wstring file) = 0;

	bool canExportAnimation() const { return m_canExportAnimation; }

	void setAnimationsToExport(std::vector<int> values) { m_animsToExport = values; }

protected:
	void exportGLTexture(GLuint id, std::wstring filename) const;
	bool m_canExportAnimation = false;
	std::vector<int> m_animsToExport;
};
