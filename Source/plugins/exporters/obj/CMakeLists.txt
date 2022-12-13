#obj_exporter lib

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building OBJ exporter")

add_definitions(-DQT_PLUGIN)

set(src OBJExporter.cpp)

set(headers OBJExporter.h)

source_group("Header Files" FILES ${headers})

set(useful_files objexporter.json)
set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

use_glew()
use_core()
use_wow()

set(NAME objexporter)
add_library(${NAME} SHARED ${src} ${headers} ${useful_files})
set_property(TARGET ${NAME} PROPERTY FOLDER "plugins")

target_link_libraries(${NAME} Qt5::Core ${extralibs})

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/plugins/")

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
