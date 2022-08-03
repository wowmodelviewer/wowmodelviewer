###################
#   common part   #
###################

# set build type you want
# available values are None Debug Release RelWithDebInfo MinSizeRel (will strip exe for minimal output size)
SET(CMAKE_BUILD_TYPE RelWithDebInfo)

# define base repo path to use it cross folder
SET(WMV_BASE_PATH ${CMAKE_SOURCE_DIR}/..)

# define cmake folder to be reusable cross scripts
SET(WMV_CMAKE_FOLDER ${WMV_BASE_PATH}/src/cmake)

# add wmv cmake directory to search path
#set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${WMV_CMAKE_FOLDER}")

# define policies to avoid warnings
include(${WMV_CMAKE_FOLDER}/policies.cmake)

# Qt5 stuff
# init cmake with our qt install directory
SET(QT_LOCATION C:/Qt/5.13.2/msvc2017)
SET(QT_VERSION 5.13.2)
SET(CMAKE_PREFIX_PATH ${QT_LOCATION}/lib/cmake)


#############################
#  platform specific part   #
#############################
if(WIN32)
  include(${WMV_BASE_PATH}/src/cmake/windows.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
  include(${WMV_BASE_PATH}/src/cmake/linux.cmake)
elseif(APPLE)
  include(${WMV_BASE_PATH}/src/cmake/macos.cmake)    
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
  find_package(Qt5Core ${QT_VERSION})
  find_package(Qt5Xml ${QT_VERSION})
  find_package(Qt5Gui ${QT_VERSION})
  find_package(Qt5Network ${QT_VERSION})
  list(APPEND extralibs wow)
endmacro()

macro(use_core)
  include_directories(${WMV_BASE_PATH}/src/core)
  link_directories(${QT_LOCATION}/lib)
  find_package(Qt5Core ${QT_VERSION}) 
  find_package(Qt5Gui ${QT_VERSION}) # Qt5Gui is needed for QImage
  find_package(Qt5Network ${QT_VERSION})
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