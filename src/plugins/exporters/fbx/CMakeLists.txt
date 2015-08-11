project(fbxexporter)
cmake_minimum_required(VERSION 2.6)

include($ENV{WMV_BASE_PATH}/src/cmake/common.cmake)

message(STATUS "Building FBX exporter")

find_package(wxWidgets REQUIRED core)
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS})

# Qt5 stuff
find_package(Qt5Core)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

include_directories($ENV{WMV_SDK_BASEDIR}/include)
include_directories($ENV{WMV_BASE_PATH}/src/wowmodelviewer) # needed for util.h inclusion

set(src FBXExporter.cpp)

use_glew()
use_core()
use_wow()

add_library(fbxexporter SHARED ${src})

target_link_libraries(fbxexporter Qt5::Core core wow $ENV{WMV_SDK_BASEDIR}/lib/libfbxsdk.lib)

if(WIN32)
  install(TARGETS fbxexporter RUNTIME DESTINATION "$ENV{WMV_BASE_PATH}/bin/plugins")
  install(FILES "$ENV{WMV_SDK_BASEDIR}/lib/libfbxsdk.dll" DESTINATION "$ENV{WMV_BASE_PATH}/bin")
endif(WIN32)
