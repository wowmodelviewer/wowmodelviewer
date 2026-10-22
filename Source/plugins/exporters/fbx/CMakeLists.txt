# fbx exporter lib
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building FBX exporter")

add_definitions(-DQT_PLUGIN)

# x64 uses the FBX SDK 2020.3 staged under ThirdParty/fbx_x64; prepend it so its fbxsdk.h wins over
# the 32-bit 2019.2 headers in ThirdParty/include (still added for the rest).
if(WMV_X64)
  include_directories(BEFORE ${CMAKE_SOURCE_DIR}/ThirdParty/fbx_x64/include)
endif()
include_directories(${CMAKE_SOURCE_DIR}/ThirdParty/include)
include_directories(${CMAKE_SOURCE_DIR}/Source/wowmodelviewer) # needed for util.h inclusion

set(src FBXExporter.cpp
  FBXCustomizationExporter.cpp
  FBXEquipmentExporter.cpp
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

if(WMV_X64)
  set(FBX_SDK_LIB ${CMAKE_SOURCE_DIR}/ThirdParty/fbx_x64/lib/libfbxsdk.lib)
  set(FBX_SDK_DLL ${CMAKE_SOURCE_DIR}/ThirdParty/fbx_x64/lib/libfbxsdk.dll)
else()
  set(FBX_SDK_LIB ${CMAKE_SOURCE_DIR}/ThirdParty/lib/libfbxsdk.lib)
  set(FBX_SDK_DLL ${CMAKE_SOURCE_DIR}/ThirdParty/lib/libfbxsdk.dll)
endif()

target_link_libraries(${NAME} Qt5::Core ${extralibs} ${FBX_SDK_LIB} ${wxWidgets_LIBRARIES})

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/plugins/")

if(WIN32)
  install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
  install(FILES "${FBX_SDK_DLL}" DESTINATION "${CMAKE_SOURCE_DIR}/bin")
endif(WIN32)
