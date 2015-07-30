project(fbxexporter)
cmake_minimum_required(VERSION 2.6)

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

message(STATUS "Building FBX exporter")

# Qt5 stuff
find_package(Qt5Core)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

include_directories($ENV{WMV_SDK_BASEDIR}/include)

file(GLOB src *.cpp )

add_library(fbxexporter SHARED ${src})

target_link_libraries(fbxexporter Qt5::Core core wow $ENV{WMV_SDK_BASEDIR}/lib/libfbxsdk.lib)

if(WIN32)
  install(TARGETS fbxexporter RUNTIME DESTINATION "${WMV_BASE_PATH}/../bin/plugins")
  install(FILES "$ENV{WMV_SDK_BASEDIR}/lib/libfbxsdk.dll" DESTINATION "${WMV_BASE_PATH}/../bin")
endif(WIN32)
