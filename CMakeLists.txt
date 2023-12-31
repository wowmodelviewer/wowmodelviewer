cmake_minimum_required(VERSION 3.16)
project(WoWModelViewer)

# Dependencies.
# Qt5
SET(QT_LOCATION C:/Qt/5.13.2/msvc2017)
SET(CMAKE_PREFIX_PATH ${QT_LOCATION}/lib/cmake)
find_package(Qt5 5.13.2 COMPONENTS Core Network Widgets Xml Gui REQUIRED)

#wxwidgets
SET(CMAKE_PREFIX_PATH ThirdParty/wxWidgets)
SET(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets COMPONENTS net aui xml adv core base REQUIRED)
# add wx widgets inclusions
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/ThirdParty/include)


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
add_subdirectory(ThirdParty/CxImage)

# configure CASC lib for our usage
option(CASC_UNICODE ON)
option(CASC_BUILD_SHARED_LIB OFF)
option(CASC_BUILD_TESTS OFF)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "Force Static library building" FORCE)
option(CASC_BUILD_STATIC_LIB ON)
set(CASCLIB_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/casclib)
set(CMAKE_INSTALL_PREFIX ${CASCLIB_INSTALL_LOCATION})
add_subdirectory(ThirdParty/casclib)
set_property(TARGET casc_static PROPERTY FOLDER "ThirdParty libraries")

# Main source files.
add_subdirectory(Source)

add_custom_target(release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target clean --config Release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target install --config Release
                  COMMAND
                  ${CMAKE_SOURCE_DIR}/Installers/NSIS/makensis.exe "${CMAKE_SOURCE_DIR}/Installers/Windows/NSIS/WMVInstallerMUI.nsi"
          )
