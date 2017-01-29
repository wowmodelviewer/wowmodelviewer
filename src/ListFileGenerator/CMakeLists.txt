project(ListFileGenerator)
include($ENV{WMV_BASE_PATH}/src/cmake/common.cmake)

cmake_minimum_required(VERSION 2.6)
message(STATUS "Building ListFileGenerator")

cmake_policy(SET CMP0020 NEW)
include_directories(.)

# Qt5 stuff
set(CMAKE_PREFIX_PATH $ENV{WMV_SDK_BASEDIR}/Qt/lib/cmake)
find_package(Qt5Core)

set(src main.cpp)
        
use_wow()

add_executable(ListFileGenerator ${src})
set_property(TARGET ListFileGenerator PROPERTY FOLDER "executables")

target_link_libraries(ListFileGenerator wow Qt5::Core)

install(TARGETS ListFileGenerator 
          RUNTIME DESTINATION $ENV{WMV_BASE_PATH}/bin)
 