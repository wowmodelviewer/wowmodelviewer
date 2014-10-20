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

# define policies to avoid warnings
include(cmake/policies.cmake)

# search for required package (wxwidgets)
find_package(wxWidgets COMPONENTS net gl aui xml adv core base REQUIRED)

# add cximage compilation 
add_subdirectory(CxImage)

# stormlib compilation  
add_subdirectory(stormlib)
set (EXTRA_LIBS ${EXTRA_LIBS} StormLib)

# CascLib compilation
add_subdirectory(casclib)
set (EXTRA_LIBS ${EXTRA_LIBS} casc_static)

# add global definitions : all warnings considered as errors
add_definitions(-g -Wall -Werror)

# add wx widgets inclusions
include(${wxWidgets_USE_FILE})

# add wmv source files compilation
include(cmake/sources.cmake)

# Qt5 stuff
# init cmake with our qt install directory
set(CMAKE_PREFIX_PATH $ENV{QT_CMAKE_DIR})

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