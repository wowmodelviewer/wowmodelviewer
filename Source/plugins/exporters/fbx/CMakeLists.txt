# fbx exporter lib
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building FBX exporter")

add_definitions(-DQT_PLUGIN)

include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/include)
include_directories(${CMAKE_SOURCE_DIR}/Source/wowmodelviewer) # needed for util.h inclusion

set(src FBXExporter.cpp
  FBXAnimExporter.cpp
  FBXHeaders.cpp)

set(headers FBXExporter.h
  FBXAnimExporter.h
  FBXHeaders.h)

source_group("Header Files" FILES ${headers})

set(useful_files fbxexporter.json)
set_source_files_properties(${useful_files} PROPERTIES HEADER_FILE_ONLY TRUE)

use_glew()
use_core()
use_wow()

set(NAME fbxexporter)
add_library(${NAME} SHARED ${src} ${headers} ${useful_files})
set_property(TARGET ${NAME} PROPERTY FOLDER "plugins")

target_link_libraries(${NAME} Qt5::Core ${extralibs} 
${CMAKE_SOURCE_DIR}/ThirdParty/lib/libfbxsdk.lib)

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/plugins/")

if(WIN32)
  install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
  install(FILES "${CMAKE_SOURCE_DIR}/ThirdParty/lib/libfbxsdk.dll" DESTINATION "${CMAKE_SOURCE_DIR}/bin")
endif(WIN32)
