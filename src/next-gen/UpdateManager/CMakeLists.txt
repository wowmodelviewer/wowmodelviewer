project(UpdateManager)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Building UpdateManager")

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

include_directories(.)
include_directories(../)

# Qt5 stuff
set(CMAKE_PREFIX_PATH D:/Programmes/QT_VS_5.4.2/5.4/msvc2013_opengl/lib/cmake)
find_package(Qt5Core)
find_package(Qt5Gui)
find_package(Qt5Widgets)
find_package(Qt5Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

file(GLOB src *.cpp)

add_executable(UpdateManager WIN32 ${src})

add_dependencies(UpdateManager core)
target_link_libraries(UpdateManager core wow Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

install(TARGETS UpdateManager 
          RUNTIME DESTINATION ${WMV_BASE_PATH}/../bin)
 