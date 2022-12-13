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

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
