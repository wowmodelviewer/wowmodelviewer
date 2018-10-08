project(obj_exporter)
cmake_minimum_required(VERSION 2.6)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/common.cmake)

message(STATUS "Building OBJ exporter")

# Qt5 stuff
find_package(Qt5Core)
#find_package(Qt5Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

set(src OBJExporter.cpp)

set(headers OBJExporter.h)

source_group("Header Files" FILES ${headers})

set(useful_files objexporter.json)
set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

use_glew()
use_core()
use_wow()

set(NAME objexporter)
add_library(${NAME} SHARED ${src} ${headers} ${useful_files})
set_property(TARGET ${NAME} PROPERTY FOLDER "plugins")

target_link_libraries(${NAME} Qt5::Core core wow)

if (MSVC_IDE)
	# Enable Qt in Visual Studio
	set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
	set(OUTPUT_DIR "../../../bin/$(Configuration)/$(PlatformName)/plugins")
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OUTPUT_DIR})
endif()

if(WIN32)
  install(TARGETS ${NAME} RUNTIME DESTINATION "${WMV_BASE_PATH}/bin/plugins")
endif(WIN32)
