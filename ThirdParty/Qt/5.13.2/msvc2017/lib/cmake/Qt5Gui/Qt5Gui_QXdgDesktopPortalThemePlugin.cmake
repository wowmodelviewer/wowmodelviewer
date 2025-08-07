
add_library(Qt5::QXdgDesktopPortalThemePlugin MODULE IMPORTED)

_populate_Gui_plugin_properties(QXdgDesktopPortalThemePlugin RELEASE "platformthemes/qxdgdesktopportal.dll")
_populate_Gui_plugin_properties(QXdgDesktopPortalThemePlugin DEBUG "platformthemes/qxdgdesktopportald.dll")

list(APPEND Qt5Gui_PLUGINS Qt5::QXdgDesktopPortalThemePlugin)
