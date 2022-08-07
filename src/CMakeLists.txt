cmake_minimum_required(VERSION 3.16)
project(WoWModelViewer)

SET(CMAKE_PREFIX_PATH
C:/Qt/5.13.2/msvc2017/lib/cmake;
${CMAKE_SOURCE_DIR}/Dependencies/wxWidgets)

# Dependencies.
# Qt5
find_package(Qt5 5.13.2 COMPONENTS Core Network Widgets Xml Gui REQUIRED)

#wxwidgets
SET(wxWidgets_USE_UNICODE ON)
find_package(wxWidgets COMPONENTS net aui xml adv core base REQUIRED)
# add wx widgets inclusions
include(${wxWidgets_USE_FILE})
include_directories(${wxWidgets_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/Dependencies/include)


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/common.cmake)

# group cmake auto targets  in a dedicated folder for Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER QTAutoMoc)

if(DEFINED BUILDNUMBER)
  add_definitions(-D_BUILDNUMBER=${BUILDNUMBER})
endif()

# add external libraries compilation
add_subdirectory(Dependencies/3rdparty/CxImage)

# configure CASC lib for our usage
set(CASCLIB_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/casclib)
add_subdirectory(Dependencies/3rdparty/casclib)

# add internal libraries compilation 
add_subdirectory(core)
add_subdirectory(games)

# then trigs executables compilation
add_subdirectory(wowmodelviewer)
add_subdirectory(UpdateManager)

# add plugins compilation
add_subdirectory(plugins)

add_custom_target(release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target clean --config Release
          COMMAND
          "${CMAKE_COMMAND}" --build . --target install --config Release
                  COMMAND
                  ${CMAKE_SOURCE_DIR}/NSIS/makensis.exe "${WMV_BASE_PATH}/Installers/Windows/NSIS/WMVInstallerMUI.nsi"
          )
