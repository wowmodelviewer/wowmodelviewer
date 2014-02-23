###################
#   common part   #
###################

project(wowmodelviewer)
cmake_minimum_required(VERSION 2.6)

# set build type you want
# availabke values are None Debug Release RelWithDebInfo MinSizeRel (will strip exe for minimal output size)
SET(CMAKE_BUILD_TYPE MinSizeRel)

# define base repo path to use it cross folder
set(WMV_BASE_PATH "${CMAKE_SOURCE_DIR}")

# add wmv cmake directory to search path
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_SOURCE_DIR}/cmake")

# search for required package (wxwidgets)
find_package(wxWidgets COMPONENTS net gl aui xml adv core base REQUIRED)

# add cximage compilation 
add_subdirectory(CxImage)

# stormlib compilation  
add_subdirectory(stormlib)
set (EXTRA_LIBS ${EXTRA_LIBS} StormLib)

# add global definitions : all warnings considered as errors
add_definitions(-g -Wall -Werror)

# add wx widgets inclusions
include(${wxWidgets_USE_FILE})

# add wmv source files compilation
include(cmake/sources.cmake)

if(POLICY CMP0020)
  cmake_policy(SET CMP0020 NEW)
endif()

# Qt5 stuff
# need to first set prefix to your Qt5 install directory
set(CMAKE_PREFIX_PATH "C:\\Qt\\5.2.0\\mingw48_32")
find_package(Qt5Core)

include_directories(next-gen)

#############################
#  platform specific part   #
#############################

if(WIN32)
  include(cmake/windows.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
  include(cmake/linux.cmake)
elseif(APPLE)
  include(cmake/macos.cmake)	  
endif()
        
# add compilation from next gen software
# after legacy part, to ensure that glew inclusio, link, etc is well defined
add_subdirectory(next-gen)