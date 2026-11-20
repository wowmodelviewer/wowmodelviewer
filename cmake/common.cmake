###################
#   common part   #
###################

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

#############################
#  platform specific part   #
#############################
if(WIN32)
  include(${CMAKE_SOURCE_DIR}/cmake/windows.cmake)
elseif(APPLE)
  add_definitions(-D_MAC)
else()
  add_definitions(-D_LINUX)
endif()

######################################
# macro to be reused across projects #
######################################
macro(use_glew)
  # The viewport is Vulkan; nothing links OpenGL any more. This keeps the vendored headers (glm) on
  # the include path for the code that used to pull them in through GLEW.
  include_directories(${CMAKE_SOURCE_DIR}/ThirdParty)
endmacro()

macro(use_cximage)
  include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/CxImage)
  list(APPEND extralibs cximage)
endmacro()

macro(use_wow)
  use_core() # if you use wow lib, you are underneath using core lib
  use_casclib() # if you use wow lib, you are underneath using casc lib 
  include_directories(${CMAKE_SOURCE_DIR}/Source/games/wow)
  link_directories(${QT_LOCATION}/lib)
  list(APPEND extralibs wow)
endmacro()

macro(use_core)
  include_directories(${CMAKE_SOURCE_DIR}/Source/core)
  link_directories(${QT_LOCATION}/lib)
  list(APPEND extralibs core)
endmacro()
  
macro(use_casclib)
  include_directories(${CASCLIB_INSTALL_LOCATION}/include)
  link_directories(${CASCLIB_INSTALL_LOCATION}/lib)
  list(APPEND extralibs casc_static)
endmacro()

macro(use_stormlib)
  include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/stormlib/src)
  list(APPEND extralibs storm)
endmacro()

macro(use_sqlite)
  list(APPEND src sqlite3.c)
endmacro()

# A plugin library (exporters, importers): Qt plugins the viewer loads from its plugin folder at start --
# <exe>/plugins on Windows and Linux, the bundle's Contents/PlugIns on macOS.
macro(wmv_plugin_output target)
  if(APPLE)
    set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "$<TARGET_BUNDLE_CONTENT_DIR:wowmodelviewer>/PlugIns")
  elseif(UNIX)
    set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:wowmodelviewer>/plugins")
  endif()
endmacro()
