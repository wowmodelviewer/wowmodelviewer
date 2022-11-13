# updatemanager exe
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building UpdateManager")

cmake_policy(SET CMP0020 NEW)
include_directories(.)

set(src main.cpp
        UpdateManager.cpp)

set(headers UpdateManager.h)

source_group("Header Files" FILES ${headers})
        
use_core()
use_wow()

set(NAME UpdateManager)
add_executable(${NAME} WIN32 ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "executables")

target_link_libraries(${NAME} ${extralibs} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")

if (MSVC_IDE)
  # Enable Qt in Visual Studio
  set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
  set_target_properties(${NAME} PROPERTIES COMPILE_DEFINITIONS $<$<CONFIG:DEBUG>:_CONSOLE>)
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:windows")
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:windows")
  set_target_properties(${NAME} PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:windows")

  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_DIR})
endif()

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})