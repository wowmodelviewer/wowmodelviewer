# wow lib
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building wow library")

set(src animated.cpp
      AnimManager.cpp
      Attachment.cpp
      Bone.cpp
      CASCFile.cpp
      CASCFolder.cpp
      CharDetails.cpp
      CharTexture.cpp
      database.cpp
      ddslib.cpp
      globalvars.cpp
      HardDriveFile.cpp
      ModelAttachment.cpp
      ModelCamera.cpp
      ModelColor.cpp
      ModelEvent.cpp
      ModelLight.cpp
      ModelManager.cpp
      ModelRenderPass.cpp
      ModelTransparency.cpp
      particle.cpp
      RaceInfos.cpp
      RenderTexture.cpp
      TabardDetails.cpp
      Texture.cpp
      TextureAnim.cpp
      TextureManager.cpp
      video.cpp
      wdb2file.cpp
      wdb5file.cpp
      wdb6file.cpp
      wdc1file.cpp
      wdc2file.cpp
      wdc3file.cpp
      wmo.cpp
      WMOFog.cpp
      WMOGroup.cpp
      WMOLight.cpp
      WMOModelInstance.cpp
      WoWDatabase.cpp
      WoWFolder.cpp
      WoWItem.cpp
      WoWModel.cpp)

set(headers animated.h
      AnimManager.h
      Attachment.h
      Bone.h
      CASCChunks.h
      CASCFile.h
      CASCFolder.h
      CharDetails.h
      CharDetailsEvent.h
      CharTexture.h
      database.h
      ddslib.h
      displayable.h
      FileTreeItem.h
      globalvars.h
      HardDriveFile.h
      manager.h
      ModelAttachment.h
      ModelCamera.h
      ModelColor.h
      ModelEvent.h
      modelheaders.h
      ModelLight.h
      ModelManager.h
      ModelRenderPass.h
      ModelTransparency.h
      particle.h
      RaceInfos.h
      RenderTexture.h
      TabardDetails.h
      TextureAnim.h
      types.h
      video.h
      wdb2file.h
      wdb5file.h
      wdb6file.h
      wdc1file.h
      wdc2file.h
      wdc3file.h
      wmo.h
      WMOFog.h
      WMOGroup.h
      WMOLight.h
      WMOModelInstance.h
      wow_enums.h
      WoWDatabase.h
      WoWFolder.h
      WoWItem.h
      WoWModel.h)

source_group("Header Files" FILES ${headers})

use_core()
use_glew()
use_casclib()

add_definitions(-DBUILDING_WOW_DLL)

set(NAME wow)
add_library(${NAME} SHARED ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "libraries")

target_link_libraries(${NAME} Qt5::Core Qt5::Xml Qt5::Gui ${extralibs} glu32)
add_dependencies(${NAME} casclib)

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")

if (MSVC_IDE)
  # Enable Qt in Visual Studio
  set_property(TARGET ${NAME} PROPERTY VS_GLOBAL_KEYWORD "Qt4VSv1.0")
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${BIN_DIR})
  set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${BIN_DIR})
endif()

if(WIN32)
  install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})
endif(WIN32)