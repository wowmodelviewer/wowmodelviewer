
add_library(Qt5::QQmlPreviewServiceFactory MODULE IMPORTED)

_populate_Qml_plugin_properties(QQmlPreviewServiceFactory RELEASE "qmltooling/qmldbg_preview.dll")
_populate_Qml_plugin_properties(QQmlPreviewServiceFactory DEBUG "qmltooling/qmldbg_previewd.dll")

list(APPEND Qt5Qml_PLUGINS Qt5::QQmlPreviewServiceFactory)
