cmake_minimum_required(VERSION 2.6)
include($ENV{WMV_BASE_PATH}/src/cmake/common.cmake)

project(core)

message(STATUS "Building nextgen Core")

include_directories(.)

# Qt5 stuff
find_package(Qt5Core) 
find_package(Qt5Gui) # Qt5Gui is needed for QImage
find_package(Qt5Network)
set(CMAKE_AUTOMOC ON)

set(src CharInfos.cpp
        ExporterPlugin.cpp
        FileDownloader.cpp
        GlobalSettings.cpp
        ImporterPlugin.cpp
        Model.cpp
        NPCInfos.cpp
        Plugin.cpp
        PluginManager.cpp
        VersionManager.cpp
        metaclasses/Component.cpp
        metaclasses/Event.cpp
        metaclasses/Observable.cpp
        metaclasses/Observer.cpp
        logger/Logger.cpp
        logger/LogOutputConsole.cpp
        logger/LogOutputFile.cpp)

use_glew()
use_cximage()

add_definitions(-DBUILDING_CORE_DLL)

add_library(core SHARED ${src})
set_property(TARGET core PROPERTY FOLDER "libraries")

target_link_libraries(core Qt5::Core Qt5::Network Qt5::Gui ${extralibs})

if(WIN32)
  install(TARGETS core RUNTIME DESTINATION "$ENV{WMV_BASE_PATH}/bin/")
endif(WIN32)
