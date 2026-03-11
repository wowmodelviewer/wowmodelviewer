cmake_minimum_required(VERSION 3.16)
project(WoWModelViewer)

message(STATUS "=== Configuring WoWModelViewer ===")

# Dependencies.
# Qt5
message(STATUS "Finding Qt5...")
set(QT_LOCATION "${CMAKE_SOURCE_DIR}/ThirdParty/Qt/5.13.2/msvc2017_64")
list(PREPEND CMAKE_PREFIX_PATH "${QT_LOCATION}/lib/cmake")
find_package(Qt5 5.13.2 COMPONENTS Core Network Widgets Xml Gui REQUIRED)
message(STATUS "  Qt5 found: ${Qt5_VERSION}")

# WxWidgets
message(STATUS "Finding wxWidgets...")
set(wxWidgets_ROOT_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets_x64")
set(wxWidgets_LIB_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/wxWidgets_x64/lib/vc_x64_lib")
set(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets COMPONENTS gl net aui xml adv core base REQUIRED)
message(STATUS "  wxWidgets found: ${wxWidgets_LIBRARIES}")
# Include WxWidgets headers
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/ThirdParty/include")


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

# group cmake auto targets  in a dedicated folder for Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER QTAutoMoc)

if(DEFINED BUILDNUMBER)
  add_definitions(-D_BUILDNUMBER=${BUILDNUMBER})
endif()

# add external libraries compilation
message(STATUS "Adding CxImage...")
add_subdirectory(ThirdParty/CxImage)

# CascLib
message(STATUS "Adding CascLib...")
set(CASC_UNICODE ON CACHE BOOL "Use Unicode in CascLib" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "Force Static library building")
set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "Compile dynamically linked library")
include_directories(ThirdParty/casclib/src)
set(CASCLIB_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/casclib)
set(CMAKE_INSTALL_PREFIX ${CASCLIB_INSTALL_LOCATION})
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(ThirdParty/casclib)
set_property(TARGET casc_static PROPERTY FOLDER "ThirdParty libraries")

# Main source files.
message(STATUS "Adding Source...")
add_subdirectory(Source)

add_custom_target(release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target clean --config Release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target install --config Release
                  COMMAND
                  ${CMAKE_SOURCE_DIR}/Installers/NSIS/makensis.exe "${CMAKE_SOURCE_DIR}/Installers/Windows/NSIS/WMVInstallerMUI.nsi"
          )
