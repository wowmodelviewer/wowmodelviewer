# wowhead lib
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building wowhead importer (static)")

set(src WowheadImporter.cpp)

set(headers WowheadImporter.h)

source_group("Header Files" FILES ${headers})

use_core()
use_wow()

set(NAME wowhead)
add_library(${NAME} STATIC ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "importers")
wmv_set_compiler_flags(${NAME})

target_link_libraries(${NAME} nlohmann_json::nlohmann_json winhttp ${extralibs})
