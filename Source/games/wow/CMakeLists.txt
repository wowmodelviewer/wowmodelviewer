# wow lib
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building wow library")

set(src animated.cpp
      AnimManager.cpp
      Attachment.cpp
      BlockDecode.cpp
      Bone.cpp
      CASCFile.cpp
      CASCFolder.cpp
      CascFileProvider.cpp
      CharDetails.cpp
      CustomizationCatalog.cpp
      CharTexture.cpp
      database.cpp
      dbdfile.cpp
      ddslib.cpp
      globalvars.cpp
      HardDriveFile.cpp
      LightCatalog.cpp
      LiquidCatalog.cpp
      Listfile.cpp
      ModelAttachment.cpp
      ModelCamera.cpp
      ModelColor.cpp
      ModelEvent.cpp
      ModelLight.cpp
      ModelManager.cpp
      ModelRenderPass.cpp
      MpqFile.cpp
      MpqFileProvider.cpp
      ModelTransparency.cpp
      particle.cpp
      RaceInfos.cpp
      RenderList.cpp
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
      WoWModel.cpp
      WorldMapCatalog.cpp
      WotlkDbc.cpp
      world/AdtModel.cpp
      world/AdtParser.cpp
      world/ByteCursor.cpp
      world/LiquidFormat.cpp
      world/TerrainLayout.cpp
      world/WdtParser.cpp
      world/WorldChunks.cpp
      world/WorldPlacements.cpp)

set(headers animated.h
      AnimManager.h
      Attachment.h
      BlockDecode.h
      Bone.h
      CASCChunks.h
      CASCFile.h
      CASCFolder.h
      CascFileProvider.h
      CharDetails.h
      CharDetailsEvent.h
      CharTexture.h
      database.h
      dbdfile.h
      ddslib.h
      displayable.h
      FileTreeItem.h
      globalvars.h
      HardDriveFile.h
      LightCatalog.h
      LiquidCatalog.h
      Listfile.h
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
      MpqFile.h
      MpqFileProvider.h
      particle.h
      RaceInfos.h
      RenderList.h
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
      WoWModel.h
      WorldMapCatalog.h
      WotlkDbc.h
      world/AdtModel.h
      world/AdtParser.h
      world/ByteCursor.h
      world/LiquidFormat.h
      world/TerrainLayout.h
      world/WdtParser.h
      world/WorldApi.h
      world/WorldChunks.h
      world/WorldPlacements.h
      world/WorldText.h)

source_group("Header Files" FILES ${headers})

use_core()
use_glew()
use_casclib()
use_stormlib()

add_definitions(-DBUILDING_WOW_DLL)

set(NAME wow)
add_library(${NAME} SHARED ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "libraries")

target_link_libraries(${NAME} Qt5::Core Qt5::Xml Qt5::Gui ${extralibs})
add_dependencies(${NAME} casc_static storm)

set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/")

install(TARGETS ${NAME} RUNTIME DESTINATION ${BIN_DIR})

# Headless checks of the world-format parsers (world/), on synthetic ADT/WDT bytes: run worldparse_test.
option(WMV_BUILD_TESTS "Build the wow library's headless test programs" ON)
if(WMV_BUILD_TESTS)
  add_executable(worldparse_test world/tests/worldparse_test.cpp)
  set_property(TARGET worldparse_test PROPERTY FOLDER "tests")
  target_include_directories(worldparse_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(worldparse_test ${NAME})
endif()
