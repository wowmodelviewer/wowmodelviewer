project(armory)

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building armory importer (static)")

set(src ArmoryImporter.cpp)

set(headers ArmoryImporter.h)

source_group("Header Files" FILES ${headers})

use_core()
use_wow()

set(NAME armory)
add_library(${NAME} STATIC ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "importers")
wmv_set_compiler_flags(${NAME})

target_link_libraries(${NAME} nlohmann_json::nlohmann_json winhttp ${extralibs})
