project(UpdateManager)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Building UpdateManager")

# define policies to avoid warnings
include(${CMAKE_SOURCE_DIR}/cmake/policies.cmake)

#set(RES_FILES "UpdateManager.rc")
#set(CMAKE_RC_COMPILER_INIT windres)
#ENABLE_LANGUAGE(RC)
#SET(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff -o <OBJECT> <SOURCE>")

include_directories(.)
include_directories(../)

# Qt5 stuff
set(CMAKE_PREFIX_PATH D:/Programmes/QT_VS/5.3/msvc2012_opengl/lib/cmake)
find_package(Qt5Core)
find_package(Qt5Gui)
find_package(Qt5Widgets)
find_package(Qt5Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

file(GLOB src *.cpp)

if(${CMAKE_BUILD_TYPE} MATCHES MinSizeRel)
  add_executable(UpdateManager WIN32 ${src} ${RES_FILES} )
else()
  # non min size release case ( = dev) => let a console attached to app
  add_executable(UpdateManager ${src} ${RES_FILES} )
endif()

add_dependencies(UpdateManager core)
target_link_libraries(UpdateManager core wow Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

install(TARGETS UpdateManager 
          RUNTIME DESTINATION ${WMV_BASE_PATH}/../bin)
 