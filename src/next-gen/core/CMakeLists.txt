project(core)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Building nextgen Core")

include_directories(.)
include_directories(../)

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Network)
set(CMAKE_AUTOMOC ON)

file(GLOB src *.cpp ../metaclasses/*.cpp ../logger/*.cpp)

add_library(core STATIC ${src})

target_link_libraries(core Qt5::Core Qt5::Network)
        
