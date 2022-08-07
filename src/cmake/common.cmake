###################
#   common part   #
###################

# define base repo path to use it cross folder
SET(WMV_BASE_PATH ${CMAKE_SOURCE_DIR}/..)

# define cmake folder to be reusable cross scripts
SET(WMV_CMAKE_FOLDER ${WMV_BASE_PATH}/src/cmake)

# define policies to avoid warnings
include(${WMV_CMAKE_FOLDER}/policies.cmake)

#############################
#  platform specific part   #
#############################
if(WIN32)
  include(${WMV_BASE_PATH}/src/cmake/windows.cmake)
endif()

######################################
# macro to be reused across projects #
######################################
macro(use_glew)
  include_directories(${CMAKE_SOURCE_DIR}/Dependencies/3rdparty)
  add_definitions(-DGLEW_STATIC)
  list(APPEND extralibs opengl32 ${CMAKE_SOURCE_DIR}/Dependencies/3rdparty/libs/glew32s.lib)
endmacro()

macro(use_cximage)
  include_directories(${CMAKE_SOURCE_DIR}/Dependencies/3rdparty/CxImage)
  list(APPEND extralibs cximage)
endmacro()

macro(use_wow)
  use_core() # if you use wow lib, you are underneath using core lib
  use_casclib() # if you use wow lib, you are underneath using casc lib 
  include_directories(${WMV_BASE_PATH}/src/games/wow)
  link_directories(${QT_LOCATION}/lib)
  list(APPEND extralibs wow)
endmacro()

macro(use_core)
  include_directories(${WMV_BASE_PATH}/src/core)
  link_directories(${QT_LOCATION}/lib)
  list(APPEND extralibs core)
endmacro()
  
macro(use_casclib)
  include_directories(${CASCLIB_INSTALL_LOCATION}/include)
  link_directories(${CASCLIB_INSTALL_LOCATION}/lib)
  list(APPEND extralibs casc)
endmacro()

macro(use_sqlite)
  list(APPEND src sqlite3.c)
endmacro()