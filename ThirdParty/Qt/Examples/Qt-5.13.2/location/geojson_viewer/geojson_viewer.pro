TARGET = qml_location_geojsonviewer
TEMPLATE = app

QT += core qml network quick positioning location-private widgets
SOURCES += main.cpp
CONFIG += c++11
CONFIG += install_ok

QT_FOR_CONFIG += location-private


RESOURCES += \
    qml.qrc

OTHER_FILES += \
    $$files(data/*.json)

target.path = $$[QT_INSTALL_EXAMPLES]/location/geojsonviewer
INSTALLS += target
DEFINES += $$shell_quote(SRC_PATH=$$PWD)
