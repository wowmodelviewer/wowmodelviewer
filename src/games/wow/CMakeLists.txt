cmake_minimum_required(VERSION 2.6)
include($ENV{WMV_BASE_PATH}/src/cmake/common.cmake)

project(wow)

message(STATUS "Building wow library")

# Qt5 stuff
find_package(Qt5Core)
find_package(Qt5Xml)
find_package(Qt5Gui)
find_package(Qt5Network)
# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

set(src animated.cpp
        AnimManager.cpp
        Attachment.cpp
        Bone.cpp
        CASCFile.cpp
        CASCFolder.cpp
        CharDetails.cpp
        CharTexture.cpp
        database.cpp
        dbcfile.cpp
        ddslib.cpp
        Game.cpp
        GameDatabase.cpp
        GameFile.cpp
        GameFolder.cpp
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
        quaternion.cpp
        RaceInfos.cpp
        RenderTexture.cpp
        TabardDetails.cpp
        TextureAnim.cpp
        video.cpp
        wmo.cpp
        WoWItem.cpp
        WoWModel.cpp)

use_core()
use_glew()
use_casclib()
use_sqlite()


add_definitions(-DBUILDING_WOW_DLL)

add_library(wow SHARED ${src})
set_property(TARGET wow PROPERTY FOLDER "libraries")

target_link_libraries(wow core CascLib Qt5::Core Qt5::Xml Qt5::Gui)

if(WIN32)
  install(TARGETS wow RUNTIME DESTINATION "${WMV_BASE_PATH}/bin/")
endif(WIN32)