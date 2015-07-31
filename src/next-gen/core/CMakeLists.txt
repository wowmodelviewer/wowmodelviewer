project(core)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Building nextgen Core")

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

include_directories(.)
include_directories(../)

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
        ../metaclasses/Component.cpp
        ../metaclasses/ConstIterator.cpp
        ../metaclasses/Event.cpp
        ../metaclasses/Iterator.cpp
        ../metaclasses/Observable.cpp
        ../metaclasses/Observer.cpp
        ../logger/Logger.cpp
        ../logger/LogOutputConsole.cpp
        ../logger/LogOutputFile.cpp)

add_definitions(-DBUILDING_CORE_DLL)

add_library(core SHARED ${src})

target_link_libraries(core ${wxWidgets_LIBRARIES} Qt5::Core Qt5::Network Qt5::Gui cximage)

if(WIN32)
  install(TARGETS core RUNTIME DESTINATION "${WMV_BASE_PATH}/../bin/")
endif(WIN32)
