cmake_minimum_required(VERSION 2.6)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake)

project(core)

message(STATUS "Building nextgen Core")

include_directories(.)

# Qt5 stuff
find_package(Qt5Core) 
find_package(Qt5Gui) # Qt5Gui is needed for QImage
find_package(Qt5Network)
find_package(Qt5Xml)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(src CharInfos.cpp
		CSVFile.cpp
		dbfile.cpp
        ExporterPlugin.cpp
        FileDownloader.cpp
		Game.cpp
		GameDatabase.cpp
		GameFile.cpp
		GameFolder.cpp
        GlobalSettings.cpp
        ImporterPlugin.cpp
		MemoryUtils.cpp
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

set(headers CharInfos.h
			CSVFile.h
			dbfile.h
			ExporterPlugin.h
			FileDownloader.h
			Game.h
			GameDatabase.h
			GameFile.h
			GameFolder.h
			GlobalSettings.h
			ImporterPlugin.h
			MemoryUtils.h
			Model.h
			NPCInfos.h
			Plugin.h
			PluginManager.h
			VersionManager.h
			metaclasses/BaseIterator.h
			metaclasses/Component.h
			metaclasses/Container.h
			metaclasses/Event.h
			metaclasses/Observable.h
			metaclasses/Observer.h
			logger/Logger.h
			logger/LogOutput.h
			logger/LogOutputConsole.h
			logger/LogOutputFile.h)

source_group("Header Files" FILES ${headers})
			
use_glew()
use_cximage()
use_sqlite()

add_definitions(-DBUILDING_CORE_DLL)

set(NAME core)
add_library(${NAME} SHARED ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "libraries")

target_link_libraries(${NAME} Qt5::Core Qt5::Network Qt5::Xml Qt5::Gui ${extralibs})

if (MSVC_IDE)
	# Enable Qt in Visual Studio
	set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
	set(OUTPUT_DIR "../bin/$(Configuration)/$(PlatformName)/")
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OUTPUT_DIR})
endif()

if(WIN32)
  install(TARGETS ${NAME} RUNTIME DESTINATION "${WMV_BASE_PATH}/bin/")
endif(WIN32)
