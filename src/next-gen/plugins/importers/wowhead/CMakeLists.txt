project(wowhead)
cmake_minimum_required(VERSION 2.6)

if(POLICY CMP0020)
  cmake_policy(SET CMP0020 NEW)
endif()


message(STATUS "Building wowhead importer")

find_package(wxWidgets COMPONENTS net core base REQUIRED)

# Qt5 stuff
# need to first set prefix to your Qt5 install directory
set(CMAKE_PREFIX_PATH "C:\\Qt\\5.2.0\\mingw48_32")
find_package(Qt5Core)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_PLUGIN)

file(GLOB src *.cpp )

add_library(wowhead SHARED ${src})

add_dependencies(wowhead core)

target_link_libraries(wowhead ${wxWidgets_LIBRARIES} Qt5::Core core)

if(WIN32)
  install(TARGETS wowhead RUNTIME DESTINATION "${WMV_BASE_PATH}/../bin/plugins")
endif(WIN32)
