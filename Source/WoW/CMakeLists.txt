# wow lib
include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

message(STATUS "Building wow library")

include_directories(CASC)
include_directories(Database)
include_directories(Model)
include_directories(Animation)
include_directories(Texture)
include_directories(Character)
include_directories(World)
include_directories(Rendering)
include_directories(Common)

set(src CASC/CASCFile.cpp
      CASC/CASCFolder.cpp
      CASC/HardDriveFile.cpp
      Database/GameDatabase.cpp
      Database/dbfile.cpp
      Database/DBDFile.cpp
      Database/WoWDatabase.cpp
      Database/DB2Table.cpp
      Database/WDCReader.cpp
      Database/wdb2file.cpp
      Database/wdb5file.cpp
      Database/wdb6file.cpp
      Database/wdc1file.cpp
      Database/wdc2file.cpp
      Database/wdc3file.cpp
      Database/wdc5file.cpp
      Database/database.cpp
      Model/WoWModel.cpp
      Model/Bone.cpp
      Model/Attachment.cpp
      Model/ModelAttachment.cpp
      Model/ModelCamera.cpp
      Model/ModelColor.cpp
      Model/ModelEvent.cpp
      Model/ModelLight.cpp
      Model/ModelManager.cpp
      Model/ModelRenderPass.cpp
      Model/ModelTransparency.cpp
      Animation/animated.cpp
      Animation/AnimManager.cpp
      Animation/particle.cpp
      Texture/CharTexture.cpp
      Texture/TextureAnim.cpp
      Texture/TextureManager.cpp
      Texture/ddslib.cpp
      Character/CharDetails.cpp
      Character/CharInfos.cpp
      Character/NPCInfos.cpp
      Character/RaceInfos.cpp
      Character/TabardDetails.cpp
      Character/WoWItem.cpp
      World/WoWFolder.cpp
      Rendering/Texture.cpp)

set(headers CASC/CASCChunks.h
      CASC/CASCFile.h
      CASC/CASCFolder.h
      CASC/HardDriveFile.h
      Database/GameDatabase.h
      Database/dbfile.h
      Database/DBDFile.h
      Database/WoWDatabase.h
      Database/DB2Table.h
      Database/WDCReader.h
      Database/wdb2file.h
      Database/wdb5file.h
      Database/wdb6file.h
      Database/wdc1file.h
      Database/wdc2file.h
      Database/wdc3file.h
      Database/wdc5file.h
      Database/database.h
      Model/WoWModel.h
      Model/Bone.h
      Model/Attachment.h
      Model/ModelAttachment.h
      Model/ModelCamera.h
      Model/ModelColor.h
      Model/ModelEvent.h
      Model/modelheaders.h
      Model/ModelLight.h
      Model/ModelManager.h
      Model/ModelRenderPass.h
      Model/ModelTransparency.h
      Animation/animated.h
      Animation/AnimManager.h
      Animation/AnimMapper.h
      Animation/particle.h
      Texture/CharTexture.h
      Texture/TextureAnim.h
      Texture/TextureManager.h
      Texture/ddslib.h
      Character/CharDetails.h
      Character/CharDetailsEvent.h
      Character/CharInfos.h
      Character/NPCInfos.h
      Character/RaceInfos.h
      Character/TabardDetails.h
      Character/WoWItem.h
      World/WoWFolder.h
      Rendering/Texture.h
      Common/wow_enums.h)

source_group("Header Files" FILES ${headers})

use_core()
use_glad()
use_casclib()

set(NAME wow)
add_library(${NAME} STATIC ${src} ${headers})
set_property(TARGET ${NAME} PROPERTY FOLDER "libraries")

target_link_libraries(${NAME} pugixml-static nlohmann_json::nlohmann_json ${extralibs} glu32)
add_dependencies(${NAME} casc_static)
