#pragma once

#include <string>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _GLOBALSETTINGS_API_ __declspec(dllexport)
#    else
#        define _GLOBALSETTINGS_API_ __declspec(dllimport)
#    endif
#else
#    define _GLOBALSETTINGS_API_
#endif

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
		GlobalSettings(GlobalSettings&);

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
