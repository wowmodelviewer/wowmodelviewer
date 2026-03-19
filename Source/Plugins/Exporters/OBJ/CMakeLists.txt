#obj_exporter lib

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building OBJ exporter (static)")

set(src OBJExporter.cpp)

set(headers OBJExporter.h)

source_group("Header Files" FILES ${headers})

use_glad()
use_core()
use_wow()

set(NAME objexporter)
add_library(${NAME} STATIC ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "exporters")

target_link_libraries(${NAME} ${extralibs})
