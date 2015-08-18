###################
#   common part   #
###################

# set build type you want
# availabke values are None Debug Release RelWithDebInfo MinSizeRel (will strip exe for minimal output size)
SET(CMAKE_BUILD_TYPE MinSizeRel)

# define base repo path to use it cross folder
string (REPLACE "\\" "/" WMV_BASE_PATH $ENV{WMV_BASE_PATH})
SET(ENV{WMV_BASE_PATH} ${WMV_BASE_PATH})

# define cmake folder to be reusable cross scripts
SET(WMV_CMAKE_FOLDER $ENV{WMV_BASE_PATH}/src/cmake)

# add wmv cmake directory to search path
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${WMV_CMAKE_FOLDER}")

# define policies to avoid warnings
include(${WMV_CMAKE_FOLDER}/policies.cmake)

# clean up a bit weird backslashes in sdk path
string (REPLACE "\\" "/" WMV_SDK_BASEDIR $ENV{WMV_SDK_BASEDIR})
SET(ENV{WMV_SDK_BASEDIR} ${WMV_SDK_BASEDIR})

# Qt5 stuff
# init cmake with our qt install directory
set(CMAKE_PREFIX_PATH $ENV{WMV_SDK_BASEDIR}/Qt/lib/cmake)

#############################
#  platform specific part   #
#############################
if(WIN32)
  include($ENV{WMV_BASE_PATH}/src/cmake/windows.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
  include($ENV{WMV_BASE_PATH}/src/cmake/linux.cmake)
elseif(APPLE)
  include($ENV{WMV_BASE_PATH}/src/cmake/macos.cmake)    
endif()

######################################
# macro to be reused across projects #
######################################
macro(use_glew)
  include_directories($ENV{WMV_BASE_PATH}/src/glew/include)
  list(APPEND src $ENV{WMV_BASE_PATH}/src/glew/src/glew.c)
  add_definitions(-DGLEW_STATIC)
  
  # temporary solution, glew needs opengl lib, and right now, wx one is used...
  # as wxwidget congig is global across cmake files, need to find all packages here :(
  set(wxWidgets_CONFIGURATION msw)
  find_package(wxWidgets REQUIRED net gl aui xml adv core base)
  list(APPEND extralibs ${wxWidgets_LIBRARIES})
endmacro()

macro(use_cximage)
  include_directories($ENV{WMV_BASE_PATH}/src/CxImage)
  list(APPEND extralibs ${wxWidgets_LIBRARIES} cximage)
endmacro()

macro(use_wow)
  include_directories($ENV{WMV_BASE_PATH}/src/games/wow)
  find_package(Qt5Core)
  find_package(Qt5Xml)
  find_package(Qt5Gui)
  find_package(Qt5Network)
endmacro()

macro(use_core)
  include_directories($ENV{WMV_BASE_PATH}/src/core)
  find_package(Qt5Core) 
  find_package(Qt5Gui) # Qt5Gui is needed for QImage
  find_package(Qt5Network)
endmacro()
  
macro(use_casclib)
  include_directories($ENV{WMV_BASE_PATH}/src/casclib/src)
endmacro()

macro(use_sqlite)
  list(APPEND src sqlite3.c)
endmacro()