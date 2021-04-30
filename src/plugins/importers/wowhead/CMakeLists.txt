project(wowhead)
cmake_minimum_required(VERSION 2.6)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/common.cmake)

message(STATUS "Building wowhead importer")

set(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets COMPONENTS net core base REQUIRED)

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

set(src WowheadImporter.cpp)

set(headers WowheadImporter.h)

source_group("Header Files" FILES ${headers})

set(useful_files wowheadimporter.json)
set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

use_wow()

set(NAME wowhead)
add_library(${NAME} SHARED ${src} ${headers} ${useful_files})
target_link_libraries(${NAME} Qt5::Core ${extralibs})

setup_wmv_plugin()

if (MSVC_IDE)
  # Enable Qt in Visual Studio
  set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_DIR})
endif()
