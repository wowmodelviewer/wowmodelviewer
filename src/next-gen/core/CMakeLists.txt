project(core)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Building nextgen Core")

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

include_directories(.)
include_directories(../)

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Network)
set(CMAKE_AUTOMOC ON)

file(GLOB src *.cpp ../metaclasses/*.cpp ../logger/*.cpp)

add_library(core STATIC ${src})

qt5_use_modules(core Core Network)
        
