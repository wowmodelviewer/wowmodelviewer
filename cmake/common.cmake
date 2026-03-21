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
endif()

# glm is a submodule; its headers live under ThirdParty/glm/glm/
include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/glm)
# glm 1.0+ requires this for gtx/ experimental extensions
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)

######################################
# macro to be reused across projects #
######################################
macro(use_glad)
  list(APPEND extralibs glad_gl opengl32)
endmacro()

macro(use_wow)
  use_core() # if you use wow lib, you are underneath using core lib
  use_casclib() # if you use wow lib, you are underneath using casc lib
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Core)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Plugin)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/CASC)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Database)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Model)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Animation)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Texture)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Character)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/World)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Rendering)
  include_directories(${CMAKE_SOURCE_DIR}/Source/WoW/Common)
  list(APPEND extralibs wow)
endmacro()

macro(use_core)
  include_directories(${CMAKE_SOURCE_DIR}/Source/Engine/Core)
  include_directories(${CMAKE_SOURCE_DIR}/Source/Engine/Core/Logger)
  include_directories(${CMAKE_SOURCE_DIR}/Source/Engine/Core/Events)
  include_directories(${CMAKE_SOURCE_DIR}/Source/Engine/Core/Containers)
  include_directories(${CMAKE_SOURCE_DIR}/Source/Engine/Core/Memory)
  include_directories(${CMAKE_SOURCE_DIR}/Source/Engine/IO)
  include_directories(${CMAKE_SOURCE_DIR}/Source/Engine/Rendering)
  include_directories(${CMAKE_SOURCE_DIR}/Source/Engine/Settings)
  include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/pugixml/src)
  include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/stb)
  list(APPEND extralibs core)
endmacro()

macro(use_casclib)
  include_directories(${CASCLIB_INSTALL_LOCATION}/include)
  link_directories(${CASCLIB_INSTALL_LOCATION}/lib)
  list(APPEND extralibs casc_static)
endmacro()

macro(use_glfw)
  list(APPEND extralibs glfw)
endmacro()

macro(use_imgui)
  use_glfw()
  list(APPEND extralibs imgui)
endmacro()
