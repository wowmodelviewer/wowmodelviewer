project(UpdateManager)
cmake_minimum_required(VERSION 2.6)

if(POLICY CMP0020)
  cmake_policy(SET CMP0020 NEW)
endif()

message(STATUS "Building UpdateManager")

set(RES_FILES "UpdateManager.rc")
set(CMAKE_RC_COMPILER_INIT windres)
ENABLE_LANGUAGE(RC)
SET(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff -o <OBJECT> <SOURCE>")

include_directories(.)
include_directories(../)

# Qt5 stuff
# need to first set prefix to your Qt5 install directory
set(CMAKE_PREFIX_PATH "C:\\Qt\\5.2.0\\mingw48_32")
find_package(Qt5 REQUIRED Core Gui Widgets Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

file(GLOB src *.cpp)

add_executable(UpdateManager ${src} ${RES_FILES})

add_dependencies(UpdateManager core)

target_link_libraries(UpdateManager core Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

install(TARGETS UpdateManager 
          RUNTIME DESTINATION ${WMV_BASE_PATH}/../bin)
 