project(wow)
cmake_minimum_required(VERSION 2.6)

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

message(STATUS "Building wow library")

# search for required package (wxwidgets)
find_package(wxWidgets COMPONENTS net gl aui xml adv core base REQUIRED)

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Xml)
find_package(Qt5Gui)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

file(GLOB src *.cpp )
list(APPEND src sqlite3.c)
list(APPEND src ${CMAKE_SOURCE_DIR}/glew/src/glew.c)

add_definitions(-DGLEW_STATIC)

include_directories(${CMAKE_SOURCE_DIR}/glew/include)

add_library(wow SHARED ${src})

target_link_libraries(wow ${wxWidgets_LIBRARIES} core casc cximage jpeg png Qt5::Core Qt5::Xml Qt5::Gui)

if(WIN32)
  install(TARGETS wow RUNTIME DESTINATION "${WMV_BASE_PATH}/../bin/")
endif(WIN32)