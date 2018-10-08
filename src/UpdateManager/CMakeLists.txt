project(UpdateManager)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake)

cmake_minimum_required(VERSION 2.6)
message(STATUS "Building UpdateManager")

cmake_policy(SET CMP0020 NEW)
include_directories(.)

# Qt5 stuff
set(CMAKE_PREFIX_PATH $ENV{WMV_SDK_BASEDIR}/Qt/lib/cmake)
find_package(Qt5Core)
find_package(Qt5Gui)
find_package(Qt5Xml)
find_package(Qt5Widgets)
find_package(Qt5Network)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

set(src main.cpp
        UpdateManager.cpp)

set(headers UpdateManager.h)

source_group("Header Files" FILES ${headers})
        
use_core()
use_wow()

set(NAME UpdateManager)
add_executable(${NAME} WIN32 ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

target_link_libraries(${NAME} core wow Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

if (MSVC_IDE)
	# Enable Qt in Visual Studio
	set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
	set_target_properties(${NAME} PROPERTIES COMPILE_DEFINITIONS $<$<CONFIG:DEBUG>:"_CONSOLE")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:CONSOLE")
	set_target_properties(${NAME} PROPERTIES COMPILE_DEFINITIONS $<$<CONFIG:RELWITHDEBINFO>:"_CONSOLE")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:windows")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:windows")

	set(OUTPUT_DIR "../bin/$(Configuration)/$(PlatformName)/")
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${OUTPUT_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OUTPUT_DIR})
endif()

install(TARGETS ${NAME} RUNTIME DESTINATION ${WMV_BASE_PATH}/bin) 