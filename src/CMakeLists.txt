###################
#   common part   #
###################

project(wowmodelviewer)
cmake_minimum_required(VERSION 2.6)

# set build type you want
# availabke values are None Debug Release RelWithDebInfo MinSizeRel (will strip exe for minimal output size)
SET(CMAKE_BUILD_TYPE RelWithDebInfo)

# add wmv cmake directory to search path
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" "${CMAKE_MODULE_PATH}")

# search for required package (wxwidgets)
find_package(wxWidgets COMPONENTS net gl aui xml adv core base REQUIRED)

# add cximage compilation 
add_subdirectory(CxImage)

# add compilation from next gen software
add_subdirectory(next-gen)

# stormlib compilation  
add_subdirectory(stormlib)
set (EXTRA_LIBS ${EXTRA_LIBS} StormLib)

# add global definitions : all warnings considered as errors
add_definitions(-g -Wall -Werror)

# add wx widgets inclusions
include(${wxWidgets_USE_FILE})

# add wmv source files compilation
include(cmake/sources.cmake)

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
        
