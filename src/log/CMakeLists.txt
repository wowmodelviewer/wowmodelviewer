cmake_minimum_required(VERSION 2.6)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake)

project(log)

message(STATUS "Building nextgen log")

include_directories(.)

# Qt5 stuff
find_package(Qt5Core) 
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(src Logger.cpp
    LogOutputConsole.cpp
    LogOutputFile.cpp)

set(headers Logger.h
      LogOutput.h
      LogOutputConsole.h
      LogOutputFile.h)

source_group("Header Files" FILES ${headers})

add_definitions(-DBUILDING_LOG_DLL)

set(NAME log)
add_library(${NAME} SHARED ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "libraries")

target_link_libraries(${NAME} Qt5::Core)

if(WIN32)
  install(TARGETS ${NAME} RUNTIME DESTINATION "${WMV_BASE_PATH}/bin/")
 endif(WIN32)
