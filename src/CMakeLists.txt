cmake_minimum_required(VERSION 3.16)
project(WoWModelViewer)

# Qt5 stuff
# init cmake with our qt install directory
SET(QT_LOCATION C:/Qt/5.13.2/msvc2017)
SET(CMAKE_PREFIX_PATH ${QT_LOCATION}/lib/cmake)
find_package(Qt5 5.13.2 COMPONENTS Core Network Widgets Xml Gui REQUIRED)

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
