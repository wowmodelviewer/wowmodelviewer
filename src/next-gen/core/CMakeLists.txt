project(core)
cmake_minimum_required(VERSION 2.6)

if(POLICY CMP0020)
  cmake_policy(SET CMP0020 NEW)
endif()

message(STATUS "Building nextgen Core")

include_directories(.)
include_directories(../)

# Qt5 stuff
# need to first set prefix to your Qt5 install directory
set(CMAKE_PREFIX_PATH "C:\\Qt\\5.2.0\\mingw48_32")
find_package(Qt5 REQUIRED Core Network)
set(CMAKE_AUTOMOC ON)

file(GLOB src *.cpp ../metaclasses/*.cpp ../logger/*.cpp)

add_library(core STATIC ${src})

target_link_libraries(core Qt5::Core Qt5::Network)
        
