project(armory)

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building armory importer")

add_definitions(-DQT_PLUGIN)

set(src ArmoryImporter.cpp)

set(headers ArmoryImporter.h)

source_group("Header Files" FILES ${headers})

set(useful_files armoryimporter.json)
set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

use_core()
use_wow()

set(NAME armory)
add_library(${NAME} SHARED ${src} ${headers} ${useful_files})
set_property(TARGET ${NAME} PROPERTY FOLDER "plugins")

target_link_libraries(${NAME} Qt5::Core ${extralibs})

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/plugins/")

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
    