project(wowhead)
cmake_minimum_required(VERSION 2.6)

include($ENV{WMV_BASE_PATH}/src/cmake/common.cmake)

message(STATUS "Building wowhead importer")

find_package(wxWidgets COMPONENTS net core base REQUIRED)
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS})

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

set(src WowheadImporter.cpp)

set(useful_files wowheadimporter.json)
set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

use_core()
use_wow()

add_library(wowhead SHARED ${src} ${useful_files})
set_property(TARGET wowhead PROPERTY FOLDER "plugins")

target_link_libraries(wowhead ${wxWidgets_LIBRARIES} Qt5::Core core wow)

if(WIN32)
  install(TARGETS wowhead RUNTIME DESTINATION "$ENV{WMV_BASE_PATH}/bin/plugins")
endif(WIN32)
