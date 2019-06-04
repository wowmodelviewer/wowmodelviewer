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
set(AUTORCC ON)

set(src main.cpp
        UpdateManager.cpp)

set(headers UpdateManager.h)

source_group("Header Files" FILES ${headers})

set (resources ../../updateManager.qrc)
source_group("Resources" FILES ${resources})
QT5_ADD_RESOURCES(QTRESOURCES ${resources})

set(TRANSLATION_PATH ${WMV_BASE_PATH}/src/Translations/)

# Translation files for the various languages
set(translationfiles ${TRANSLATION_PATH}updatemanager_en_US.ts)

source_group("Translation Files" FILES ${translationfiles})

find_package(Qt5LinguistTools REQUIRED)
set(LUPDATE_OPTIONS "-ts ${translationfiles}" CACHE STRING "specifies options passed to lupdate")
# adds the translations and a target for it
qt5_create_translation(QM_FILES
	${src} ${headers}
	OPTIONS ${LUPDATE_OPTIONS}
)

source_group("GeneratedFiles" FILES ${QTRESOURCES})

use_core()
use_wow()

set(NAME UpdateManager)
add_executable(${NAME} WIN32 ${src} ${headers} ${translationfiles} ${resources} ${QTRESOURCES})
set_property(TARGET ${NAME} PROPERTY FOLDER "executables")
set_property(TARGET ${NAME} PROPERTY CXX_STANDARD 17)

target_link_libraries(${NAME} core wow Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

set(BIN_DIR "${WMV_BASE_PATH}/bin/")

if (MSVC_IDE)
	# Enable Qt in Visual Studio
	set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
	set_target_properties(${NAME} PROPERTIES COMPILE_DEFINITIONS $<$<CONFIG:DEBUG>:_CONSOLE>)
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:windows")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:windows")
	set_target_properties(${NAME} PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:windows")

	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${BIN_DIR})
	set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_DIR})
endif()

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})