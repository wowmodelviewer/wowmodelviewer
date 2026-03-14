#pragma once

#include <string>

#define _GLOBALSETTINGS_API_

#define GLOBALSETTINGS core::GlobalSettings::instance()

namespace core
{
	class _GLOBALSETTINGS_API_ GlobalSettings
	{
	public:
		~GlobalSettings();

		static GlobalSettings& instance()
		{
			if (GlobalSettings::m_instance == nullptr)
				GlobalSettings::m_instance = new GlobalSettings();

			return *m_instance;
		}

		std::wstring appVersion(std::wstring a_prefix = std::wstring(L""));
		std::wstring appName();
		std::wstring buildName();
		std::wstring appTitle();

		bool isBeta() { return m_isBetaVersion; }

		bool bShowParticle;
		bool bZeroParticle;
		bool bInitPoseOnlyExport;

	private:
		GlobalSettings();
		GlobalSettings(const GlobalSettings&) = delete;
		GlobalSettings& operator=(const GlobalSettings&) = delete;

		int m_versionMajorNumber;
		int m_versionMinorNumber;
		int m_versionRevNumber;

		std::wstring m_appName;
		std::wstring m_buildName;
		std::wstring m_platform;

		bool m_isBetaVersion;
		bool m_isAlphaVersion;

		static GlobalSettings* m_instance;
	};
}
