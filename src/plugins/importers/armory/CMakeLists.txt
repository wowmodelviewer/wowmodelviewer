project(armory)
cmake_minimum_required(VERSION 2.6)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/common.cmake)

message(STATUS "Building armory importer")

# Qt5 stuff
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

set(src ArmoryImporter.cpp)

set(headers ArmoryImporter.h)

source_group("Header Files" FILES ${headers})

set(useful_files armoryimporter.json)
set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

use_core()
use_wow()

add_library(armory SHARED ${src} ${headers} ${useful_files})
set_property(TARGET armory PROPERTY FOLDER "plugins")

target_link_libraries(armory core wow Qt5::Core)

if(WIN32)
  install(TARGETS armory RUNTIME DESTINATION "${WMV_BASE_PATH}/bin/plugins")
endif(WIN32)


        