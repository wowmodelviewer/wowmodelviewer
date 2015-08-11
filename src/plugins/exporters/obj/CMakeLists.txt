project(obj_exporter)
cmake_minimum_required(VERSION 2.6)

include($ENV{WMV_BASE_PATH}/src/cmake/common.cmake)

message(STATUS "Building OBJ exporter")

find_package(wxWidgets REQUIRED core)
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS})

# Qt5 stuff
find_package(Qt5Core)
#find_package(Qt5Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

set(src OBJExporter.cpp)

use_core()
use_wow()
use_glew()

add_library(objexporter SHARED ${src})

add_dependencies(objexporter core)

target_link_libraries(objexporter Qt5::Core core wow)

if(WIN32)
  install(TARGETS objexporter RUNTIME DESTINATION "$ENV{WMV_BASE_PATH}/bin/plugins")
endif(WIN32)
