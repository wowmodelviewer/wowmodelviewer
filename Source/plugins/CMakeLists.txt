# importers
add_subdirectory(importers/armory)
add_subdirectory(importers/wowhead)

#exporters
add_subdirectory(exporters/obj)

# The FBX exporter needs the Autodesk FBX SDK, which is not part of the 64-bit build's tree (ThirdParty/fbx_x64): it is
# built only where the SDK is (or when asked for), so a build without it still completes. 1.0 ships without it.
if(WMV_X64)
  set(WMV_FBX_SDK_LIB ${CMAKE_SOURCE_DIR}/ThirdParty/fbx_x64/lib/libfbxsdk.lib)
else()
  set(WMV_FBX_SDK_LIB ${CMAKE_SOURCE_DIR}/ThirdParty/lib/libfbxsdk.lib)
endif()
if(EXISTS ${WMV_FBX_SDK_LIB})
  set(WMV_FBX_DEFAULT ON)
else()
  set(WMV_FBX_DEFAULT OFF)
endif()
option(WMV_BUILD_FBX_EXPORTER "Build the FBX exporter plugin (needs the Autodesk FBX SDK)" ${WMV_FBX_DEFAULT})
if(WMV_BUILD_FBX_EXPORTER)
  add_subdirectory(exporters/fbx)
else()
  message(STATUS "FBX exporter not built (no FBX SDK at ${WMV_FBX_SDK_LIB})")
endif()
