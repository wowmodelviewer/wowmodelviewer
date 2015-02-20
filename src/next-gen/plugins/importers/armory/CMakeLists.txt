project(armory)
cmake_minimum_required(VERSION 2.6)

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

message(STATUS "Building armory importer")

find_package(wxWidgets COMPONENTS net core base REQUIRED)

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

file(GLOB src *.cpp )

add_library(armory SHARED ${src})

target_link_libraries(armory ${wxWidgets_LIBRARIES} core Qt5::Core)

if(WIN32)
  install(TARGETS armory RUNTIME DESTINATION "${WMV_BASE_PATH}/../bin/plugins")
endif(WIN32)


        