project(UpdateManager)
include($ENV{WMV_BASE_PATH}/src/cmake/common.cmake)

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
        
use_core()
use_wow()

add_executable(UpdateManager WIN32 ${src})

target_link_libraries(UpdateManager core wow Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

install(TARGETS UpdateManager 
          RUNTIME DESTINATION $ENV{WMV_BASE_PATH}/bin)
 